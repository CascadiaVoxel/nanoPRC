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

// Applies your existing bloom, ACES tone mapping, and gamma correction to a sample
vec3 processSample(vec2 uv)
{
    vec3 colorLinear = texture(uTexture, uv).rgb;

    if (!uWireframe && !uFullbright)
    {
        if (uEnableBloom)
        {
            vec3 bloom = texture(uBloom, uv).rgb;
            colorLinear = mix(colorLinear, bloom, uBloomStrength);
        }

        if (uEnableToneMapping)
        {
            colorLinear = aces(colorLinear);
        }
    }

    return pow(max(colorLinear, vec3(0.0)), vec3(1.0 / uGamma));
}

float luma(vec3 rgb)
{
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(uTexture, 0));

    // Sample center and 4 diagonal neighbors (processed to perceptual gamma space)
    vec3 rgbM  = processSample(TexCoords);
    vec3 rgbNW = processSample(TexCoords + vec2(-1.0, -1.0) * texelSize);
    vec3 rgbNE = processSample(TexCoords + vec2( 1.0, -1.0) * texelSize);
    vec3 rgbSW = processSample(TexCoords + vec2(-1.0,  1.0) * texelSize);
    vec3 rgbSE = processSample(TexCoords + vec2( 1.0,  1.0) * texelSize);

    float lumaM  = luma(rgbM);
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;

    // Early exit on flat regions (no edge contrast)
    const float kEdgeThresholdMin = 0.0312;
    const float kEdgeThresholdMax = 0.125;

    if (lumaRange < max(kEdgeThresholdMin, lumaMax * kEdgeThresholdMax))
    {
        FragColor = vec4(rgbM, 1.0);
        return;
    }

    // Compute edge direction perpendicular to the local luminance gradient
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    const float kReduceMin = 1.0 / 128.0;
    const float kReduceMul = 1.0 / 8.0;
    const float kSpanMax   = 8.0;

    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * kReduceMul), kReduceMin);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    dir = clamp(dir * rcpDirMin, vec2(-kSpanMax), vec2(kSpanMax)) * texelSize;

    // Blend along the detected edge
    vec3 rgbA = 0.5 * (
        processSample(TexCoords + dir * (1.0 / 3.0 - 0.5)) +
        processSample(TexCoords + dir * (2.0 / 3.0 - 0.5))
    );

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        processSample(TexCoords + dir * -0.5) +
        processSample(TexCoords + dir *  0.5)
    );

    float lumaB = luma(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax)
    {
        FragColor = vec4(rgbA, 1.0);
    }
    else
    {
        FragColor = vec4(rgbB, 1.0);
    }
}
