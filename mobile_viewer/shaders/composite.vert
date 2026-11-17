#version 300 es

layout (location = 0) in vec4 aVertex; // <vec2 pos, vec2 tex>

out vec2 TexCoords;

void main()
{
    TexCoords = aVertex.zw;
    gl_Position = vec4(aVertex.xy, 0.0, 1.0);
}
