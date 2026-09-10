#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 worldPosition;
out vec3 worldNormal;
out vec2 texCoord;
out vec4 color;

void main()
{
    worldPosition = (matModel * vec4(vertexPosition, 1.0)).xyz;
    worldNormal = mat3(matNormal) * vertexNormal;
    texCoord = vertexTexCoord;
    color = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
