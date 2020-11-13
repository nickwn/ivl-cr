#version 430
#pragma include("common.glsl")
#pragma include("materials.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(binding = 1) uniform sampler3D rawVolume; // TODO: remove
layout(binding = 2) uniform sampler1D transferLUT;
//layout(binding = 3) uniform sampler1D opacityLUT;
layout(binding = 3) uniform sampler3D sigmaVolume;
layout(binding = 4) uniform samplerCube cubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform sampler1D clearcoatLUT; // TODO: replace with cubic function?
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

    /*return -vec3(
        texture(opacityLUT, highVals.x).r - texture(opacityLUT, lowVals.x).r,
        texture(opacityLUT, highVals.y).r - texture(opacityLUT, lowVals.y).r,
        texture(opacityLUT, highVals.z).r - texture(opacityLUT, lowVals.z).r
    );*/

    return -vec3(highVals.x - lowVals.x, highVals.y - lowVals.y, highVals.z - lowVals.z);
}

const float farT = 5.0; // hehe
const float stepSize = 0.001;
const float densityScale = 0.01;
const float lightingMult = 1.0;

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

        float sigmaT = textureLod(sigmaVolume, rel, 0).a;

        sum += sigmaT * stepSize;
        isect.x += stepSize;
    }

    density = texture(rawVolume, rel).r;
    opacity = textureLod(sigmaVolume, rel, 0).a / density;
}

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);
    uint sampleNum = index.x % numSamples;

    vec4 accumPk = imageLoad(accumTex, index);
    vec3 accum = accumPk.rgb;
    if (length(accum) < 0.0001) return;

    // TODO: seed better
    initRNG(index, itrs * numSamples + sampleNum);

    vec4 rayPosPk = imageLoad(rayPosTex, index);

    vec3 ro = rayPosPk.xyz;
    vec3 rd = vec3(
        sin(rayPosPk.w) * cos(accumPk.w),
        sin(rayPosPk.w) * sin(accumPk.w),
        cos(rayPosPk.w)
    );
    vec3 startPos = ro;

    vec2 isect = rayBox(ro, rd, lowerBound, -1.0 * lowerBound);
    isect.x = max(0, isect.x);
    isect.y = min(isect.y, farT);

    // early out if no bb hit
    if (isect.x >= isect.y)
    {
        vec3 missCol = texture(cubemap, rd).rgb * accum * lightingMult;
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

    vec4 lastImgVal = imageLoad(imgOutput, index);
    if (hit == 0)
    {
        vec4 invItr = vec4(1.0 / abs(lastImgVal.a));
        vec3 missCol = texture(cubemap, rd).rgb * (1 - hit) * accum * lightingMult;
        vec4 newCol = lastImgVal * (1.0 - invItr) + vec4(missCol, 1.0) * invItr;
        newCol.a = abs(lastImgVal.a) + 1.f;

        imageStore(imgOutput, index, newCol);
        imageStore(accumTex, index, vec4(0.f));
        return;
    }

    vec3 col = textureLod(sigmaVolume, rel, 0).rgb;

    // shade with brdf or phase function (but rn just brdf)
    vec3 wo = -rd, grad = calcGradient(rel), pct = vec3(0.0), f = vec3(0.0), thpt = vec3(0.f);
    float pbrdf = pBRDF(opacity, length(grad), 1.0), pdf = 0.f, wiDotN = 1.f;
    bool goodSample = true;

    // reservoir state
    vec4 wi = vec4(0.0); // y
    if (noise() < pbrdf)
    {
        const float alpha = 0.9;
        vec3 n = normalize(grad), wm = vec3(0.f);
        if (noise() < .5f)
        {
            wi.xyz = SampleDisneyClearcoat(wo, n, wm, alpha);
            wiDotN = dot(wi.xyz, n);
            wi.w = 1.f;
        }
        else
        {
            wi.xyz = sampleLambertian(n);
            wiDotN = dot(wi.xyz, n);
            wi.w = -1.f;
        }

        // TODO: find out where these stupid rare nans are coming from
        float d, absDotNL, pClearcoat = texture(clearcoatLUT, density).r;
        f = vec3(EvaluateDisneyClearcoat(pClearcoat, alpha, wo, wm, wi.xyz, n, d));
        pct = f * wiDotN;
        pdf = clearcoatPDF(d, dot(wo, wm));
        thpt += pdf == 0.0 || isnan(pdf) || any(isnan(pct)) ? vec3(0.0) : pct / pdf;

        f = lambertian(col);
        pdf = lambertianPDF(wiDotN);
        pct = f * wiDotN;
        thpt += pdf == 0.0 || isnan(pdf) || any(isnan(pct)) ? vec3(0.0) : pct / pdf;
    }
    else
    {
        wi.xyz = sampleSchlickPhase(wo, noise2());
        f = schlickPhase(col, wi.xyz, wo, 0.0, pdf);
        pct = f;
        wi.w = -1.f;

        thpt = pct / pdf;
    }

    if (wiDotN < 0.f)
    {
        accum = vec3(0.f);
    }
    
    imageStore(imgOutput, index, vec4(lastImgVal.rgb, lastImgVal.a * wi.w));

    rayPosPk.xyz = ps;
    rayPosPk.w = acos(wi.z);
    imageStore(rayPosTex, index, rayPosPk);

    float phiOff = wi.x < 0.0 ? pi : 0.0;
    accumPk.rgb = accum * thpt;
    accumPk.a = phiOff + atan(wi.y / wi.x);
    imageStore(accumTex, index, accumPk);
}