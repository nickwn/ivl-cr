#version 430

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(rgba16f, binding = 6) uniform image2D denoisedImg;

float luminance(vec3 col)
{
    return 0.299 * col.r + 0.587 * col.g + 0.114 * col.b;
}

void main()
{
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);

    // modified from https://alain.xyz/blog/raytracing-denoising#bluring-kernels
    const int radius = 2; //5x5 kernel
    vec2 sigmaVariancePair = vec2(0.0, 0.0);
    float sampCount = 0.0;
    vec3 avgColor = vec3(0.f);
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            // We already have the center data
            if (x == 0 && y == 0) { continue; }

            // Sample current point data with current uv
            ivec2 p = index + ivec2(x, y);
            vec3 curColor = imageLoad(imgOutput, p).rgb;

            // Determine the average brightness of this sample
            // Using International Telecommunications Union's ITU BT.601 encoding params
            avgColor += curColor;
            float samp = luminance(curColor);
            float sampSquared = samp * samp;
            sigmaVariancePair += vec2(samp, sampSquared);

            sampCount += 1.0;
        }
    }
    vec3 idxColor = imageLoad(imgOutput, index).rgb;
    avgColor /= sampCount;
    sigmaVariancePair /= sampCount;

    float variance = sigmaVariancePair.y - sigmaVariancePair.x * sigmaVariancePair.x;
    float clampVariance = clamp(variance * 10.0, 0.0, 1.0);
    vec3 mixColor = avgColor * clampVariance + idxColor * (1.0 - clampVariance);
    imageStore(denoisedImg, index, vec4(mixColor, 1.0));
}