#version 300 es

layout (location = 0) in vec4 aVertex;
layout (location = 1) in vec4 aColor;

out vec4 Color;
out vec2 TexCoords;

uniform mat4 u_view;

void main()
{
    Color = aColor;
    TexCoords = aVertex.zw;
    gl_Position = u_view * vec4(aVertex.xy, 0.0, 1.0);
}
