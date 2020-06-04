#version 430
#pragma include("common.glsl")
#pragma include("materials.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(rgba32f, binding = 7) uniform image2D reservoir;
uniform uint numSamples;

vec2 sampleDisc(float radius)
{
    float r = sqrt(noise()) * radius;
    float theta = noise() * twoPi;
    return vec2(r * cos(theta), r * sin(theta));
}

void loadReservoirAtIndex(ivec2 index, out vec3 r,
    out float m, out float w, out float pHatY, out float dist,
    out vec4 rayPosPk, out vec4 accumPk)
{
    vec4 reservoirPk = imageLoad(reservoir, index);
   
    // TODO: two loads for ray dir is messy
    rayPosPk = imageLoad(rayPosTex, index);
    accumPk = imageLoad(accumTex, index);

    r = vec3(
        sin(rayPosPk.w) * cos(accumPk.w),
        sin(rayPosPk.w) * sin(accumPk.w),
        cos(rayPosPk.w)
    );

    m = reservoirPk.x;
    w = reservoirPk.y;
    pHatY = reservoirPk.z;
}

void main()
{
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);
    vec2 screenPosToIdx = 1.f / vec2(float(numSamples), 1.f);

    vec3 r = vec3(0.0);
    float m = 0.0, w = 0.0, pHatY = 0.0, dist = 0.0;
    vec4 rayPosPk = vec4(0.0), accumPk = vec4(0.0);
    loadReservoirAtIndex(index, r, m, w, pHatY, dist, rayPosPk, accumPk);
    
    if (length(accumPk.rgb) <= 0.0001) return;

    float wsum = pHatY * w * m;
    for (int i = 0; i < 0; i++)
    {
        vec2 sampPos = vec2(screenIndex) + sampleDisc(30.0);
        ivec2 sampIdx = ivec2(sampPos * screenPosToIdx);

        vec3 ar = vec3(0.0);
        vec4 aRayPosPk = vec4(0.0), aAccumPk = vec4(0.0);
        float am = 0.0, aw = 0.0, apHatY = 0.0, adist = 0.0;
        loadReservoirAtIndex(sampIdx, ar, am, aw, apHatY, adist, aRayPosPk, aAccumPk);

        if (length(aAccumPk.rgb) > 0.0001)
        {
            float weight = apHatY * aw * am;
            wsum += weight;

            if (noise() < (weight / wsum))
            {
                r = ar; pHatY = apHatY;
            }

            m += am;
        }
    }
    
    w = (1.0 / pHatY) * (wsum / m);

    float phiOff = r.x < 0.0 ? pi : 0.0;
    rayPosPk.a = acos(r.z);
    accumPk.a = phiOff + atan(r.y / r.x);
    
    accumPk.rgb = accumPk.rgb * w;

    imageStore(rayPosTex, index, rayPosPk);
    imageStore(accumTex, index, accumPk);
}