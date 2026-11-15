#version 330 core

layout (location = 0) out vec4 FragColor;

in vec3 FragPos;


uniform vec3 uSkyColorHorizon;
uniform vec3 uSkyColorZenith;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform float uSunTightness;

/* Colours in a PRC file, a texture or the settings are display (sRGB) values;
   light them as linear values, which the compositor's gamma turns back. */
vec3 toLinear(vec3 c)
{
    return pow(max(c, vec3(0.0)), vec3(2.2));
}

void main()
{
    /* Calculate the sky color */
    float cosTheta = max(dot(normalize(FragPos), vec3(0.0, 1.0, 0.0)), 0.0);
    vec3 color = mix(toLinear(uSkyColorHorizon), toLinear(uSkyColorZenith), cosTheta);

    /* Draw the sun */
    float intensity = max(dot(normalize(FragPos), normalize(-uSunDir)), 0.0);
    intensity = pow(intensity, uSunTightness);
    color += uSunColor * intensity * uSunIntensity;

    FragColor = vec4(color, 1.0);
}
