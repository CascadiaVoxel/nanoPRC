#version 300 es

layout (location = 0) in vec3 aPos;

out vec3 FragPos;
out vec3 ViewPos;

uniform mat4 uSkyView;
uniform mat4 uProjection;

void main()
{
    FragPos = aPos;
    gl_Position = uProjection * uSkyView * vec4(aPos, 1.0);
    gl_Position.z = gl_Position.w; // set depth to w
}
