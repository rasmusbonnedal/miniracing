# Todo

## HDR image-based lighting

- [x] Enable HDR image loading in Raylib (disabled by default).
- [x] Integrate the HDR environment over the hemisphere to produce diffuse irradiance (nine spherical-harmonic coefficients computed at startup).
- [x] Add a shader that evaluates irradiance using each surface's normal and multiplies it by the material's base color.
- [x] Generate terrain normals and handle linear color, exposure, and tone mapping (smooth normals, exposure in EV, Reinhard/ACES fitted controls saved in imgui.ini).
- [x] Add specular IBL with CDF-based importance sampling of the HDR environment, using material roughness and metallic values (CDF/GGX prefiltering, split-sum BRDF lookup, and view-dependent reflections).
