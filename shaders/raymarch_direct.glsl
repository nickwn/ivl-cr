﻿
#version 430
#pragma include("common.glsl")
#pragma include("materials.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
//layout(binding = 1) uniform sampler3D rawVolume; // TODO: remove
//layout(binding = 2) uniform sampler1D transferLUT;
//layout(binding = 2) uniform samplerCube irrCubemap; // replacing transferLUT
layout(binding = 3) uniform sampler3D sigmaVolume;
layout(binding = 4) uniform samplerCube irrCubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform sampler1D clearcoatLUT; // TODO: replace with cubic function?
uniform uint numSamples;
uniform vec3 scaleFactor;
uniform vec3 lowerBound;
uniform int itrs;

// from Trevor Headstrom's code
vec2 rayBox(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
    vec3 id = 1 / rd;
    vec3 t0 = (mn - ro) * id;
    vec3 t1 = (mx - ro) * id;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

const float farT = 5.0; // hehe
const float stepSize = 0.001;
const float densityScale = 0.01;
const float lightingMult = 1.0;

void trace(in vec3 ro, in vec3 rd, in vec2 isect, bool diffuse, out vec3 transmittance)
{
    const float coneSpread = 0.325f;
    const float voxelSize = stepSize; // ??
    const float mipmapHardcap = 5.4f;

    isect.x = noise() * stepSize;
    float multiplier = diffuse ? coneSpread / voxelSize : 0.f;
    float additive = diffuse ? 0.f : stepSize;
    vec3 linearDensity = vec3(0.0f);
    while (isect.x < isect.y)
    {
        vec3 ps = ro + isect.x * rd;
        vec3 rel = (ps - lowerBound) * scaleFactor;

        float l = additive + multiplier * isect.x;
        float level = log2(l);

        vec4 bakedVal = textureLod(sigmaVolume, rel, min(mipmapHardcap, level));
        vec3 sigmaT = vec3(pow(bakedVal.a, 1.5) * l) * (vec3(1.f) - bakedVal.rgb);
        
        linearDensity += sigmaT;
        isect.x += l * 2.f;
    }
    transmittance = exp(vec3(-linearDensity * 10.f));
}

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);

    vec4 accumPk = imageLoad(accumTex, index);
    vec3 accum = accumPk.rgb;
    if (length(accum) < 0.0001) return;

    // TODO: seed better
    initRNG(index, itrs);

    vec4 rayPosPk = imageLoad(rayPosTex, index);

    vec3 ro = rayPosPk.xyz;
    vec3 rd = vec3(
        sin(rayPosPk.w) * cos(accumPk.w),
        sin(rayPosPk.w) * sin(accumPk.w),
        cos(rayPosPk.w)
    );

    vec2 isect = rayBox(ro, rd, lowerBound, -1.0 * lowerBound);
    isect.x = max(0, isect.x);
    isect.y = min(isect.y, farT);

    // early out if no bb hit (shouldn't happen)
    if (isect.x >= isect.y)
    {
        return;
    }

    // init tracing from camera
    ro += rd * isect.x;
    isect.y -= isect.x;

    vec4 lastImgVal = imageLoad(imgOutput, index);

    vec3 transmittance;
    trace(ro, rd, isect, lastImgVal.a < 0.f, transmittance);

    vec4 invItr = vec4(1.f / abs(lastImgVal.a));
    vec3 incoming = textureLod(irrCubemap, rd, 7.416f).rgb * transmittance * accum;
    vec4 newCol = lastImgVal * (1.f - invItr) + vec4(incoming, 1.f) * invItr;

    float add = lastImgVal.a < 0.f ? 1.f : 0.f;
    newCol.a = abs(lastImgVal.a) + add;

    imageStore(imgOutput, index, newCol);
}