#version 300 es
precision highp float;
precision highp int;

layout (location = 0) out vec4 OutColor;

in vec4 Color;
in vec2 TexCoords;

uniform sampler2D u_texture;
uniform bool u_has_texture;
uniform bool u_is_text;

void main()
{
    vec4 color = Color;

    if (u_is_text)
        color.a *= texture(u_texture, TexCoords).r;
    else if (u_has_texture)
        color *= texture(u_texture, TexCoords);

    OutColor = color;
}
