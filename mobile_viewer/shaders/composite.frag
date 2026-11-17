#version 300 es
precision highp float;
precision highp int;

out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D uTexture;

uniform bool uEnableBloom;
uniform float uBloomStrength;
uniform sampler2D uBloom;

uniform bool uEnableToneMapping;
uniform float uExposure;

uniform float uGamma;

uniform bool uWireframe;
uniform bool uFullbright;

vec3 aces(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    x *= uExposure;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    /* Base color */
    vec3 colorLinear = texture(uTexture, TexCoords).rgb;
    
    if (!uWireframe && !uFullbright)
    {
        /* Add bloom effect */
        if (uEnableBloom)
        {
            vec3 bloom = texture(uBloom, TexCoords).rgb;
            colorLinear = mix(colorLinear, bloom, uBloomStrength);
        }

        /* Tone mapping */
        if (uEnableToneMapping)
        {
            colorLinear = aces(colorLinear);
        }
    }

    /* Gamma correction */
    vec3 colorGammaCorrected = pow(colorLinear, vec3(1.0 / uGamma));

    FragColor = vec4(colorGammaCorrected, 1.0);
}
