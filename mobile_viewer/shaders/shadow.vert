#version 300 es

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal; // Ignored
layout (location = 2) in vec3 aColor; // Ignored
layout (location = 3) in vec2 aTexCoords; // Ignored

uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

void main()
{
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
