#version 430
#pragma include("common.glsl")
#pragma include("materials.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(binding = 1) uniform sampler3D rawVolume;
layout(binding = 2) uniform sampler1D transferLUT;
layout(binding = 3) uniform sampler1D opacityLUT;
layout(binding = 4) uniform samplerCube irrCubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform samplerCube cubemap;
layout(binding = 8) uniform sampler1D clearcoatLUT;
uniform uint numSamples;
uniform vec3 scaleFactor;
uniform vec3 lowerBound;
uniform mat4 view;
uniform int itrs;
uniform uint depth;

// from Trevor Headstrom's code
vec2 rayBox(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
    vec3 id = 1 / rd;
    vec3 t0 = (mn - ro) * id;
    vec3 t1 = (mx - ro) * id;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

vec3 calcGradient(vec3 tuv)
{
    vec3 highVals = vec3(
        textureOffset(rawVolume, tuv, ivec3(1, 0, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, 1, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, 0, 1)).r
    );

    vec3 lowVals = vec3(
        textureOffset(rawVolume, tuv, ivec3(-1, 0, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, -1, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, 0, -1)).r
    );

    return -vec3( 
        texture(opacityLUT, highVals.x).r - texture(opacityLUT, lowVals.x).r,
        texture(opacityLUT, highVals.y).r - texture(opacityLUT, lowVals.y).r,
        texture(opacityLUT, highVals.z).r - texture(opacityLUT, lowVals.z).r
    );
}

const float farT = 5.0; // hehe
const float stepSize = 0.01;
const float densityScale = 0.01;

void trace(in vec3 ro, in vec3 rd, in vec2 isect, out uint hit, out vec3 ps, out vec3 rel, out float density, out float opacity)
{
    float s = -log(noise()) * densityScale;
    float sum = 0.0f;
    isect.x = noise() * stepSize;
    hit = 1;
    while (sum < s)
    {
        ps = ro + isect.x * rd;
        rel = (ps - lowerBound) * scaleFactor;

        if (isect.x >= isect.y)
        {
            hit = 0;
            break;
        }

        density = texture(rawVolume, rel).r;
        opacity = texture(opacityLUT, density).r;

        float sigmaT = density * opacity;

        sum += sigmaT * stepSize;
        isect.x += stepSize;
    }
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
    int seed1 = hash(index.x);
    int seed2 = hash(index.y);
    int seed3 = hash(itrs);
    state[0] = seed1 ^ seed3;
    state[1] = seed2 ^ seed3;
    state[2] = index.x ^ seed2;
    state[3] = index.y ^ seed1;
    noise(); noise();

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

    // early out if no bb hit
    if (isect.x >= isect.y)
    {
        vec3 missCol = texture(irrCubemap, rd).rgb * accum;
        imageStore(imgOutput, index, vec4(missCol, 1.0));
        imageStore(accumTex, index, vec4(0.f));
        return;
    }

    // init woodcock tracking from camera
    ro += rd * isect.x;
    isect.y -= isect.x;

    uint hit = 1;
    vec3 ps = vec3(0.0), rel = vec3(0.0);
    float density = 0.0, opacity = 0.0;
    trace(ro, rd, isect, hit, ps, rel, density, opacity);

    vec4 invItr = vec4(1.0 / float(itrs));
    vec3 missCol = texture(irrCubemap, rd).rgb * (1-hit) * accum;
    vec4 newCol = imageLoad(imgOutput, index);

    // mix if ray was computed as mix (imgOutput.a == 0)
    if (newCol.a == 0.0)
    {
        newCol = newCol * (1.0 - invItr) + vec4(missCol, 1.0) * invItr;
    }
    else
    {
        newCol = newCol + vec4(missCol, 1.0) * 0.5 * invItr;
    }

    if (hit == 0)
    {
        imageStore(imgOutput, index, newCol);
        imageStore(accumTex, index, vec4(0.f));
        return;
    }

    vec3 col = texture(transferLUT, density).rgb;

    // shade with brdf or phase function (but rn just brdf)
    vec3 grad = calcGradient(rel);
    vec2 uv = noise2();
    vec3 wo = -rd, wi = vec3(0.0), pct = vec3(0.0);
    float pbrdf = pBRDF(opacity, length(grad), 1.0), pdf = 0.f;
    bool additive = false;
    if (noise() < pbrdf)
    {
        vec3 n = normalize(grad);
        float pClearcoat = texture(clearcoatLUT, density).r;
        if (noise() < pClearcoat && itrs > 1)
        {
            vec3 wm;
            wi = SampleDisneyClearcoat(wo, n, wm);
            pct = vec3(EvaluateDisneyClearcoat(pClearcoat, 0.9, wo, wm, wi, n, pdf));
            additive = true;
        }
        else
        {
            wi = sampleLambertian(n, uv);
            pct = lambertian(col, wi, n, pdf);
        }
    }
    else
    {
        wi = sampleSchlickPhase(wi, uv);
        pct = schlickPhase(col, wi, wo, -0.3, pdf);
    }

    newCol.a = additive ? 1.0 : 0.0;
    imageStore(imgOutput, index, newCol);

    vec3 thpt = pct / pdf;
    rayPosPk.xyz = ps;
    rayPosPk.w = acos(wi.z);
    imageStore(rayPosTex, index, rayPosPk);

    float phiOff = wi.x < 0.0 ? pi : 0.0;
    accumPk.rgb = accum * thpt; 
    accumPk.a = phiOff + atan(wi.y / wi.x);
    imageStore(accumTex, index, accumPk);

}