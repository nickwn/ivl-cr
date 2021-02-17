
#version 430
#pragma include("common.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(binding = 3) uniform sampler3D sigmaVolume;
layout(binding = 4) uniform samplerCube cubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform sampler2D clearcoatLUT; // TODO: replace with cubic function?
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
const float lightingMult = 1.0;
vec3 helperSigmaT = vec3(0.f);

void trace(in vec3 ro, in vec3 rd, in vec2 isect, in float diffuse, out vec3 transmittance)
{
    const float coneSpread = 0.325f;
    const float voxelSize = stepSize; // ??
    const float mipmapHardcap = 5.4f;

    isect.x = rand() * stepSize;
    float multiplier = diffuse * (coneSpread / voxelSize);
    float additive = (diffuse < .5f) ? 0.f : stepSize;
    vec3 linearDensity = vec3(0.0f);
    while (isect.x < isect.y)
    {
        vec3 uvw = ro + isect.x * rd;

        float l = multiplier * isect.x + stepSize * (diffuse > .5f ? 1.f : 10.f);
        float level = log2(l);

        vec4 bakedVal = textureLod(sigmaVolume, uvw, min(mipmapHardcap, level));
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

    vec2 isect = rayBox(ro, rd, vec3(0.f), vec3(1.f));
    isect.x = max(0, isect.x);
    isect.y = min(isect.y, farT);

    // init tracing from camera
    vec4 lastImgVal = imageLoad(imgOutput, index);

    vec3 transmittance;
    float diffuse = (-1.f * sign(lastImgVal.a) + 1.f) * .5f; // 1.f if it should use voxel cone tracing, 0.f for clearcoat
    trace(ro, rd, isect, diffuse, transmittance);

    // 1 / sampleCount
    vec4 invItr = vec4(1.f / abs(lastImgVal.a));

    float cubemapLod = diffuse * 7.416f;
    vec3 incoming = textureLod(cubemap, rd, cubemapLod).rgb * transmittance * accum;
    vec4 newCol = lastImgVal * (1.f - invItr) + vec4(incoming, 1.f) * invItr;

    // add 1 to the sample count if this is not clearcoat (since clearcoat is additive and it's should not count towards mixed samples)
    float add = lastImgVal.a < 0.f ? 1.f : 0.f;
    newCol.a = abs(lastImgVal.a) + add;

    imageStore(imgOutput, index, newCol);
}