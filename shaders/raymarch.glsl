#version 430
#pragma include("common.glsl")
#pragma include("materials.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(r16, binding = 1) uniform image3D rawVolume;
layout(binding = 2) uniform sampler1D transferLUT;
layout(rgba16, binding = 3) uniform image3D sigmaVolume;
layout(binding = 4) uniform samplerCube cubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform sampler1D clearcoatLUT; // TODO: replace with cubic function?
uniform uint numSamples;
uniform vec3 scaleFactor;
uniform vec3 scanSize;
uniform vec3 scanResolution;
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

vec3 calcGradient(ivec3 tuv)
{
    vec3 highVals = vec3(
        imageLoad(sigmaVolume, tuv + ivec3(1, 0, 0)).a,
        imageLoad(sigmaVolume, tuv + ivec3(0, 1, 0)).a,
        imageLoad(sigmaVolume, tuv + ivec3(0, 0, 1)).a
    );

    vec3 lowVals = vec3(
        imageLoad(sigmaVolume, tuv + ivec3(-1, 0, 0)).a,
        imageLoad(sigmaVolume, tuv + ivec3(0, -1, 0)).a,
        imageLoad(sigmaVolume, tuv + ivec3(0, 0, -1)).a
    );

    return -highVals + lowVals;
}

const float farT = 5.0; // hehe
const float stepSize = 0.001;
const float densityScale = 0.005;
const float lightingMult = 1.0;

void trace(in vec3 ro, in vec3 rd, in vec2 isect, out uint hit, out vec3 uvw, out ivec3 idx)
{
    float s = -log(rand()) * densityScale;
    isect.x = rand() * stepSize;

    // compute ray in index space
    ro = (ro - lowerBound) * scaleFactor;
    rd *= scaleFactor;

    isect = rayBox(ro, rd, vec3(0.f), vec3(1.f));
    isect.x = max(0.f, isect.x);
    isect.y = min(3.f, isect.y);

    vec3 texelSize = 1 / scanResolution;
    float eps = min(min(texelSize.x, texelSize.y), texelSize.z) / 64;

    texelSize *= sign(rd);

    hit = 1;
    while (s > 0.f)
    {
        if (isect.x >= isect.y)
        {
            hit = 0;
            break;
        }

        uvw = ro + isect.x * rd;
        idx = ivec3(uvw * scanSize);

        vec2 ti = rayBox(ro, rd, uvw, uvw + texelSize);
        float dt = max(0.f, ti.y - ti.x);

        float sigmaT = imageLoad(sigmaVolume, idx).a;

        s -= sigmaT * dt;
        isect.x += dt + eps;
    }

    uvw = (uvw / scaleFactor) + lowerBound;
}

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);
    
    initRNG(index, itrs);
    
    vec4 accumPk = imageLoad(accumTex, index);
    vec3 accum = accumPk.rgb;
    vec4 rayPosPk = imageLoad(rayPosTex, index);

    // 3 pieces of data are packed into two textures here:
    // ro is in rayPosPk.xyz, rd is in spherical coordinates phi and theta

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
        vec3 missCol = texture(cubemap, rd).rgb * lightingMult;
        imageStore(imgOutput, index, vec4(missCol, 1.0));
        imageStore(accumTex, index, vec4(0.f));
        return;
    }

    // init woodcock tracking from camera
    ro += rd * isect.x;
    isect.y -= isect.x;

    uint hit = 1;
    vec3 uvw = vec3(0.0);
    ivec3 idx = ivec3(0);
    trace(ro, rd, isect, hit, uvw, idx);

    float density = imageLoad(rawVolume, idx).r;
    float opacity = imageLoad(sigmaVolume, idx).a / density;

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

    vec3 col = imageLoad(sigmaVolume, idx).rgb;

    // shade with brdf or phase function (but rn just brdf)
    vec2 uv = rand2();
    vec3 wo = -rd, grad = calcGradient(idx), pct = vec3(0.0), f = vec3(0.0), thpt = vec3(0.f);
    float pbrdf = pBRDF(opacity, length(grad), 1.0), pdf = 0.f, wiDotN = 1.f;
    bool goodSample = true;

    // reservoir state
    vec4 wi = vec4(0.0); // y
    if (rand() < pbrdf)
    {
        const float alpha = 0.9, pClearcoat = texture(clearcoatLUT, density).r;
        vec3 n = normalize(grad), wm = vec3(0.f);
        if (rand() < .5f)
        {
            wi.xyz = SampleDisneyClearcoat(wo, n, wm, alpha, uv);
            wiDotN = dot(wi.xyz, n);
            wi.w = 1.f;
        }
        else
        {
            wi.xyz = sampleLambertian(n, uv);
            wm = normalize(wi.xyz + wo);
            wiDotN = dot(wi.xyz, n);
            wi.w = -1.f;
        }

        float d, absDotNL;
        f = vec3(EvaluateDisneyClearcoat(pClearcoat, alpha, wo, wm, wi.xyz, n, d));
        pct = f * wiDotN;
        pdf = clearcoatPDF(d, dot(wo, wm));
        // TODO: lambertian sampled rays sometimes have invalid wm
        thpt += abs(dot(wo, wm)) < 0.0001f ? vec3(0.0) : pct / pdf;

        f = lambertian(col);
        pdf = lambertianPDF(wiDotN);
        pct = f * wiDotN;
        thpt += pct / pdf;
    }
    else
    {
        wi.xyz = sampleSchlickPhase(wo, uv);
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

    rayPosPk.xyz = uvw;
    rayPosPk.w = acos(wi.z);
    imageStore(rayPosTex, index, rayPosPk);

    float phiOff = wi.x < 0.0 ? pi : 0.0;
    accumPk.rgb = accum * thpt;
    accumPk.a = phiOff + atan(wi.y / wi.x);
    imageStore(accumTex, index, accumPk);
}