#version 300 es

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
layout (location = 3) in vec2 aTexCoords;
layout (location = 4) in vec3 aDiffuseColor;
layout (location = 5) in vec3 aMaterialTint;
layout (location = 6) in vec3 aSpecularColor;
layout (location = 7) in vec3 aEmissiveColor;
layout (location = 8) in float aShininess;
layout (location = 9) in float aAlpha;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out vec3 Color;
out vec4 FragPosLightSpace;
out vec3 VDiffuseColor;
out vec3 VMaterialTint;
out vec3 VSpecularColor;
out vec3 VEmissiveColor;
out float VShininess;
out float VAlpha;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;

/* Colours in a PRC file, a texture or the settings are display (sRGB) values;
   light them as linear values, which the compositor's gamma turns back. */
vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);

    FragPos = worldPos.xyz;
    Normal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
    TexCoords = aTexCoords;
    Color = aColor;
    VDiffuseColor = toLinear(aDiffuseColor);
    VMaterialTint = aMaterialTint;
    VSpecularColor = toLinear(aSpecularColor);
    VEmissiveColor = toLinear(aEmissiveColor);
    VShininess = aShininess;
    VAlpha = aAlpha;

    FragPosLightSpace = uLightSpaceMatrix * worldPos;

    gl_Position = uProjection * uView * worldPos;
}
