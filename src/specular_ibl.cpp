#include "specular_ibl.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <numbers>

namespace {
constexpr double pi = std::numbers::pi;
double radical_inverse(uint32_t bits) {
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xaaaaaaaau) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xccccccccu) >> 2);
    bits = ((bits & 0x0f0f0f0fu) << 4) | ((bits & 0xf0f0f0f0u) >> 4);
    bits = ((bits & 0x00ff00ffu) << 8) | ((bits & 0xff00ff00u) >> 8);
    return (bits + 0.5) / 4294967296.0;
}
double solid_angle(int row, int w, int h) {
    return 2*pi/w * (std::cos(pi*row/h) - std::cos(pi*(row+1)/h));
}
Vector3 direction(double u, double v) {
    const double theta = pi*v, phi = 2*pi*u-pi;
    return {float(std::sin(theta)*std::cos(phi)), float(std::cos(theta)), float(std::sin(theta)*std::sin(phi))};
}
Vector2 uv(Vector3 d) {
    return {float(std::atan2(d.z,d.x)/(2*pi)+.5), float(std::acos(std::clamp(double(d.y),-1.0,1.0))/pi)};
}
Vector3 read_rgb(const float* rgb, int w, int h, Vector3 d) {
    const auto t = uv(d);
    const double x = t.x*w-.5, y = t.y*h-.5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const float fx = float(x-ix), fy = float(y-iy);
    const auto pixel = [&](int a,int b) {
        a = (a%w+w)%w; b = std::clamp(b,0,h-1);
        const float* p = rgb+(b*w+a)*3;
        return Vector3 {p[0],p[1],p[2]};
    };
    return Vector3Lerp(Vector3Lerp(pixel(ix,iy),pixel(ix+1,iy),fx),
        Vector3Lerp(pixel(ix,iy+1),pixel(ix+1,iy+1),fx),fy);
}
Vector3 ggx_half(double u, double v, double alpha) {
    const double cos_theta = std::sqrt((1-v)/(1+(alpha*alpha-1)*v));
    const double sin_theta = std::sqrt(std::max(0.0,1-cos_theta*cos_theta));
    return {float(std::cos(2*pi*u)*sin_theta),float(std::sin(2*pi*u)*sin_theta),float(cos_theta)};
}
double ggx_pdf(double no_l, double alpha) {
    // V=N, NoH^2=(1+NoL)/2, p(L)=D(H)/4.
    const double a2 = alpha*alpha, term = (1+no_l)*.5*(a2-1)+1;
    return a2/(4*pi*term*term);
}
double smith(double cosine, double alpha) {
    return 2*cosine/(cosine+std::sqrt(alpha*alpha+(1-alpha*alpha)*cosine*cosine));
}
}

std::vector<double> environment_cdf(const float* rgb, int w, int h) {
    if (!rgb || w<=0 || h<=0) return {};
    std::vector<double> cdf(size_t(w)*h);
    double total = 0;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        const size_t i = size_t(y)*w+x;
        const float* p = rgb+i*3;
        cdf[i] = std::max(0.0,.2126*p[0]+.7152*p[1]+.0722*p[2])*solid_angle(y,w,h);
        total += cdf[i];
    }
    double sum = 0;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        const size_t i=size_t(y)*w+x;
        const double uniform = solid_angle(y,w,h)/(4*pi);
        sum += total>0 ? .99*cdf[i]/total+.01*uniform : uniform;
        cdf[i]=sum;
    }
    cdf.back()=1;
    return cdf;
}

SpecularIblData prefilter_specular_ibl(const float* rgb, int w, int h, int output_width, int samples) {
    SpecularIblData out;
    if (!rgb || w<=0 || h<=0 || output_width<4 || samples<2) return out;
    out.width=output_width; out.height=output_width/2;
    const int stride=out.width+2, rows=out.height+2;
    out.atlas.resize(size_t(stride)*rows*out.levels*3);
    const auto cdf=environment_cdf(rgb,w,h);
    const auto mass = [&](size_t i) { return cdf[i]-(i ? cdf[i-1] : 0); };
    const auto env_pdf = [&](Vector3 l) {
        const auto t=uv(l);
        const int x=std::clamp(int(t.x*w),0,w-1), y=std::clamp(int(t.y*h),0,h-1);
        return mass(size_t(y)*w+x)/solid_angle(y,w,h);
    };
    const auto env_sample = [&](double u,double v) {
        const size_t i=std::min(size_t(std::upper_bound(cdf.begin(),cdf.end(),u)-cdf.begin()),cdf.size()-1);
        const int row=int(i/w), col=int(i%w);
        const double fraction=(u-(i ? cdf[i-1] : 0))/mass(i);
        const double cos_theta=std::lerp(std::cos(pi*row/h),std::cos(pi*(row+1)/h),v);
        return direction((col+fraction)/w,std::acos(std::clamp(cos_theta,-1.0,1.0))/pi);
    };
    const int half_samples=samples/2;
    // Each slice writes a disjoint region; share the read-only CDF/panorama.
    std::vector<std::future<void>> workers;
    for (int level=0;level<out.levels;++level) {
        workers.push_back(std::async(std::launch::async, [&,level] {
        const double roughness=double(level)/(out.levels-1), alpha=roughness*roughness;
        for (int y=0;y<out.height;++y) for (int x=0;x<out.width;++x) {
            const Vector3 n=direction((x+.5)/out.width,(y+.5)/out.height);
            Vector3 value=read_rgb(rgb,w,h,n);
            if (level>0) {
                const Vector3 up=std::abs(n.y)<.999f ? Vector3 {0,1,0} : Vector3 {1,0,0};
                const Vector3 tangent=Vector3Normalize(Vector3CrossProduct(up,n));
                const Vector3 bitangent=Vector3CrossProduct(n,tangent);
                double sums[3] {}, weights=0;
                for (int i=0;i<half_samples;++i) for (int technique=0;technique<2;++technique) {
                    const double u=(i+.5)/half_samples, v=radical_inverse(i);
                    Vector3 l;
                    if (technique==0) l=env_sample(u,v);
                    else {
                        const Vector3 local=ggx_half(u,v,alpha);
                        const Vector3 half=Vector3Add(Vector3Add(Vector3Scale(tangent,local.x),Vector3Scale(bitangent,local.y)),Vector3Scale(n,local.z));
                        l=Vector3Normalize(Vector3Subtract(Vector3Scale(half,2*Vector3DotProduct(n,half)),n));
                    }
                    const double no_l=std::clamp(double(Vector3DotProduct(n,l)),-1.0,1.0);
                    if (no_l<=0) continue;
                    const double pdf=ggx_pdf(no_l,alpha);
                    // Balance-heuristic MIS: equal numbers of CDF and GGX samples.
                    const double weight=no_l*pdf/(.5*(pdf+env_pdf(l)));
                    const auto radiance=read_rgb(rgb,w,h,l);
                    sums[0]+=radiance.x*weight; sums[1]+=radiance.y*weight; sums[2]+=radiance.z*weight;
                    weights+=weight;
                }
                if (weights>0) value={float(sums[0]/weights),float(sums[1]/weights),float(sums[2]/weights)};
            }
            float* dst=out.atlas.data()+((level*rows+y+1)*stride+x+1)*3;
            dst[0]=value.x;dst[1]=value.y;dst[2]=value.z;
        }
        // Wrap longitude and clamp latitude in a one-texel guard band.
        for (int y=0;y<rows;++y) for (int x=0;x<stride;++x) {
            if (x>0 && x<stride-1 && y>0 && y<rows-1) continue;
            const int sx=x==0 ? out.width : (x==stride-1 ? 1 : x);
            const int sy=std::clamp(y,1,out.height);
            const float* src=out.atlas.data()+((level*rows+sy)*stride+sx)*3;
            float* dst=out.atlas.data()+((level*rows+y)*stride+x)*3;
            std::copy_n(src,3,dst);
        }
        }));
    }
    for (auto& worker : workers) worker.get();
    // Split-sum BRDF integral: coordinates are NoV (x), perceptual roughness (y).
    out.brdf.resize(size_t(out.lut_size)*out.lut_size*3);
    for (int y=0;y<out.lut_size;++y) for (int x=0;x<out.lut_size;++x) {
        const double nv=(x+.5)/out.lut_size, rough=(y+.5)/out.lut_size, alpha=rough*rough;
        const Vector3 v={float(std::sqrt(1-nv*nv)),0,float(nv)};
        double a=0,b=0;
        for (int i=0;i<samples;++i) {
            const Vector3 half=ggx_half((i+.5)/samples,radical_inverse(i),alpha);
            const double vh=std::max(0.0,double(Vector3DotProduct(v,half)));
            const Vector3 l=Vector3Subtract(Vector3Scale(half,float(2*vh)),v);
            if (l.z<=0) continue;
            const double visibility=smith(nv,alpha)*smith(l.z,alpha)*vh/(half.z*nv);
            const double fresnel=std::pow(1-vh,5);
            a+=(1-fresnel)*visibility;b+=fresnel*visibility;
        }
        const size_t index=(y*out.lut_size+x)*3;
        out.brdf[index]=float(a/samples);out.brdf[index+1]=float(b/samples);
    }
    return out;
}

bool load_specular_ibl(const char* path, Shader shader, SpecularIbl& result) {
    Image image=LoadImage(path);
    if (!image.data) return false;
    bool valid=image.format==PIXELFORMAT_UNCOMPRESSED_R32G32B32 && image.width==2*image.height;
    if (valid) {
        const float* pixels=static_cast<const float*>(image.data);
        for (size_t i=0;i<size_t(image.width)*image.height*3;++i)
            if (!std::isfinite(pixels[i]) || pixels[i]<0) { valid=false; break; }
    }
    if (!valid) { UnloadImage(image); return false; }
    TraceLog(LOG_INFO,"IBL: Prefiltering specular environment with CDF/GGX importance sampling");
    auto data=prefilter_specular_ibl(static_cast<const float*>(image.data),image.width,image.height);
    UnloadImage(image);
    Image atlas={data.atlas.data(),data.width+2,(data.height+2)*data.levels,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32};
    Image brdf={data.brdf.data(),data.lut_size,data.lut_size,1,PIXELFORMAT_UNCOMPRESSED_R32G32B32};
    result.atlas=LoadTextureFromImage(atlas);result.brdf=LoadTextureFromImage(brdf);
    if (!result.atlas.id || !result.brdf.id) {
        if (result.atlas.id) UnloadTexture(result.atlas);
        if (result.brdf.id) UnloadTexture(result.brdf);
        result={};return false;
    }
    for (auto texture : {result.atlas,result.brdf}) {
        SetTextureFilter(texture,TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(texture,TEXTURE_WRAP_CLAMP);
    }
    shader.locs[SHADER_LOC_MAP_HEIGHT]=GetShaderLocation(shader,"specularAtlas");
    shader.locs[SHADER_LOC_MAP_BRDF]=GetShaderLocation(shader,"brdfLut");
    const Vector3 layout={float(data.width),float(data.height),float(data.levels)};
    SetShaderValue(shader,GetShaderLocation(shader,"specularLayout"),&layout,SHADER_UNIFORM_VEC3);
    const int enabled=1;
    SetShaderValue(shader,GetShaderLocation(shader,"specularEnabled"),&enabled,SHADER_UNIFORM_INT);
    return true;
}

void attach_specular_ibl(Model& model, const SpecularIbl& ibl) {
    for (int i=0;i<model.materialCount;++i) {
        // Height is unused by this renderer; use its 2D texture binding for the atlas.
        model.materials[i].maps[MATERIAL_MAP_HEIGHT].texture=ibl.atlas;
        model.materials[i].maps[MATERIAL_MAP_BRDF].texture=ibl.brdf;
    }
}

void draw_ibl_model(const Model& model, Vector3 position, float rotation, Vector3 scale, Color tint, bool wires) {
    const Matrix placement=MatrixMultiply(MatrixMultiply(MatrixScale(scale.x,scale.y,scale.z),
        MatrixRotateY(rotation*DEG2RAD)),MatrixTranslate(position.x,position.y,position.z));
    const Matrix transform=MatrixMultiply(model.transform,placement);
    if (wires) rlEnableWireMode();
    for (int i=0;i<model.meshCount;++i) {
        Material material=model.materials[model.meshMaterial[i]];
        const Color original=material.maps[MATERIAL_MAP_ALBEDO].color;
        material.maps[MATERIAL_MAP_ALBEDO].color={
            (unsigned char)(int(original.r)*tint.r/255),(unsigned char)(int(original.g)*tint.g/255),
            (unsigned char)(int(original.b)*tint.b/255),(unsigned char)(int(original.a)*tint.a/255)};
        if (wires) material.shader={rlGetShaderIdDefault(),rlGetShaderLocsDefault()};
        else if (material.shader.id!=rlGetShaderIdDefault()) {
            const float roughness=std::clamp(material.maps[MATERIAL_MAP_ROUGHNESS].value,0.0f,1.0f);
            const float metallic=std::clamp(material.maps[MATERIAL_MAP_METALNESS].value,0.0f,1.0f);
            SetShaderValue(material.shader,GetShaderLocation(material.shader,"roughness"),&roughness,SHADER_UNIFORM_FLOAT);
            SetShaderValue(material.shader,GetShaderLocation(material.shader,"metallic"),&metallic,SHADER_UNIFORM_FLOAT);
        }
        DrawMesh(model.meshes[i],material,transform);
        material.maps[MATERIAL_MAP_ALBEDO].color=original;
    }
    if (wires) rlDisableWireMode();
}
