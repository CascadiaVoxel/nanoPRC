#version 300 es
precision highp float;
precision highp int;

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D uTexture;
uniform vec2 uSize;

void main()
{
    vec2 texelSize = 1.0 / uSize;
    float x = texelSize.x;
    float y = texelSize.y;

    vec3 a = texture(uTexture, vec2(TexCoords.x - 2 * x, TexCoords.y + 2 * y)).rgb;
    vec3 b = texture(uTexture, vec2(TexCoords.x, TexCoords.y + 2 * y)).rgb;
    vec3 c = texture(uTexture, vec2(TexCoords.x + 2 * x, TexCoords.y + 2 * y)).rgb;

    vec3 d = texture(uTexture, vec2(TexCoords.x - 2 * x, TexCoords.y)).rgb;
    vec3 e = texture(uTexture, TexCoords).rgb;
    vec3 f = texture(uTexture, vec2(TexCoords.x + 2 * x, TexCoords.y)).rgb;

    vec3 g = texture(uTexture, vec2(TexCoords.x - 2 * x, TexCoords.y - 2 * y)).rgb;
    vec3 h = texture(uTexture, vec2(TexCoords.x, TexCoords.y - 2 * y)).rgb;
    vec3 i = texture(uTexture, vec2(TexCoords.x + 2 * x, TexCoords.y - 2 * y)).rgb;

    vec3 j = texture(uTexture, vec2(TexCoords.x - x, TexCoords.y + y)).rgb;
    vec3 k = texture(uTexture, vec2(TexCoords.x + x, TexCoords.y + y)).rgb;
    vec3 l = texture(uTexture, vec2(TexCoords.x - x, TexCoords.y - y)).rgb;
    vec3 m = texture(uTexture, vec2(TexCoords.x + x, TexCoords.y - y)).rgb;

    vec3 color = e * 0.125;
    color += (a + c + g + i) * 0.03125;
    color += (b + d + f + h) * 0.0625;
    color += (j + k + l + m) * 0.0625;

    FragColor = vec4(color, 1.0);
}
