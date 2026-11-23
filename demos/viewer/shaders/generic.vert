#version 330 core

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
/* 1 where the vertex has texture coordinates; 0 where the attribute is not set */
layout (location = 10) in float aUvSet;

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
out float VUvSet;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpaceMatrix;
/* The size in pixels of the viewport being drawn to; 0 until the viewer sets it */
uniform vec2 uViewport;

/* Colours in a PRC file, a texture or the settings are display (sRGB) values;
   light them as linear values, which the compositor's gamma turns back. */
vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);

    /* A markup vertex that is placed relative to an anchor point, as markup text and the lines of a Frame Draw
       block are, carries in aNormal: the offset from the anchor in x and y, and in z a code no unit normal can
       have, which says how the offset is taken:
         2 Face View:  the offset is in model units, along the screen's right and up directions, so the
                       text always faces the viewer (a billboard) and scales with the model;
         3 Frame Draw: the offset is in pixels from where the anchor projects to on the screen;
         4 Fixed Size: as 3, so the size does not change as the view zooms.
       aPos is the anchor. */
    float markupMode = aNormal.z;
    bool placed = markupMode > 1.5;
    vec4 placedClip = vec4(0.0);

    if (placed)
    {
        if (markupMode < 2.5)
        {
            vec3 right = vec3(uView[0][0], uView[1][0], uView[2][0]);
            vec3 up = vec3(uView[0][1], uView[1][1], uView[2][1]);
            worldPos = vec4(worldPos.xyz + right * aNormal.x + up * aNormal.y, 1.0);
            placedClip = uProjection * uView * worldPos;
        }
        else
        {
            placedClip = uProjection * uView * worldPos;
            if (uViewport.x > 0.0 && uViewport.y > 0.0)
                placedClip.xy += aNormal.xy * (2.0 / uViewport) * placedClip.w;
            /* A little toward the viewer: a label anchored on a surface is not hidden by it */
            placedClip.z -= 0.002 * placedClip.w;
        }
    }

    FragPos = worldPos.xyz;
    Normal = placed ? vec3(0.0, 0.0, 1.0) : normalize(mat3(transpose(inverse(uModel))) * aNormal);
    TexCoords = aTexCoords;
    Color = aColor;
    VDiffuseColor = toLinear(aDiffuseColor);
    VMaterialTint = aMaterialTint;
    VSpecularColor = toLinear(aSpecularColor);
    VEmissiveColor = toLinear(aEmissiveColor);
    VShininess = aShininess;
    VAlpha = aAlpha;
    VUvSet = aUvSet;

    FragPosLightSpace = uLightSpaceMatrix * worldPos;

    gl_Position = placed ? placedClip : uProjection * uView * worldPos;
}
