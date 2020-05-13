#version 430
#pragma include("common.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(binding = 1) uniform sampler3D rawVolume;
layout(binding = 2) uniform sampler1D transferLUT;
layout(binding = 3) uniform sampler1D opacityLUT;
layout(binding = 4) uniform samplerCube irrCubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform samplerCube cubemap;
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

// from https://graphics.pixar.com/library/OrthonormalB/paper.pdf
// why does pixar have an 8 page paper on calculating an ONB
void ONB(const vec3 n, out vec3 b1, out vec3 b2)
{
    const float sign = sign(n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = vec3(b, sign + n.y * n.y * a, -n.y);
}

// surface brdf functions
vec3 lambertian(vec3 color, vec3 wo, vec3 n)
{
    return color * max(0.0, dot(wo, n)) / pi;
}

// clearcoat
// https://schuttejoe.github.io/post/disneybsdf/
float pow5(float x) 
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float Fresnel(float F0, float cosA) 
{
    return F0 + (1 - F0) * pow5(1 - cosA);
}

float GTR1(float absDotHL, float a)
{
    if (a >= 1) {
        return invPi;
    }

    float a2 = a * a;
    return (a2 - 1.0) / (pi * log2(a2) * (1.0 + (a2 - 1.0) * absDotHL * absDotHL));
}

float SeparableSmithGGXG1(vec3 w, vec3 n, float a)
{
    float a2 = a * a;
    float absDotNV = abs(dot(w, n));

    return 2.0 / (1.0 + sqrt(a2 + (1 - a2) * absDotNV * absDotNV));
}

float EvaluateDisneyClearcoat(float clearcoat, float alpha, vec3 wo, vec3 wm, vec3 wi, vec3 n)
{
    if (clearcoat <= 0.0) {
        return 0.0;
    }

    float absDotNH = abs(dot(wm, n));
    float absDotNL = abs(dot(wi, n));
    float absDotNV = abs(dot(wo, n));
    float dotHL = dot(wm, wi);

    float d = GTR1(absDotNH, mix(0.1, 0.001, alpha));
    float f = Fresnel(0.04, dotHL);
    float gl = SeparableSmithGGXG1(wi, n, 0.25);
    float gv = SeparableSmithGGXG1(wo, n, 0.25);

    //fPdfW = d / (4.0f * absDotNL);
    //rPdfW = d / (4.0f * absDotNV);

    return 0.25 * clearcoat * d * f * gl * gv;
}

vec3 sampleLambertian(vec3 n, vec2 uv)
{
    vec3 b1, b2;
    ONB(n, b1, b2);

    float r = sqrt(uv.x);
    float theta = 2 * pi * uv.y;

    float x = r * cos(theta);
    float y = r * sin(theta);

    vec3 s = vec3(x, y, sqrt(max(0.0, 1 - uv.x)));
    return normalize(s.x * b1 + s.y * b2 + s.z * n);
}

// volume phase function
vec3 schlickPhase(vec3 color, vec3 wo, vec3 wi, float k)
{
    float cosTheta = dot(wi, -wo);
    float a = (1 + k * cosTheta);
    return color * (1 - k * k) / (4 * pi * a * a);
    //return vec3(2.0) * (1 - k * k) / (4 * pi * a * a);
}

// doesn't work
vec3 sampleSchlickPhase(vec3 wi, vec2 uv)
{
    float theta = twoPi * uv.x;
    float phi = acos(2.0 * uv.y - 1.0);

    return vec3(
        sin(phi) * cos(theta),
        sin(phi) * sin(theta),
        cos(phi)
    );
}

// probability that brdf will be used vs phase function
float pBRDF(float opacity, float gradMag, float g)
{
    return opacity * (1.0 - pow(10.0, -25 * g * g * g * gradMag));
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

const vec2 screenRes = vec2(1280.0, 720.0); // todo: make uniform
const vec2 halfRes = screenRes * 0.5;
const float z = 1.0 / tan(radians(45.0) * 0.5);
const float farT = 5.0; // hehe

const float stepSize = 0.01;
const float maxAccum = 2.0;
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
    if (depth == 1)
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
    vec3 wi = -rd, wo = vec3(0.0), pct = vec3(0.0);
    float pbrdf = pBRDF(opacity, length(grad), 1.0);
    if (noise() < 1.0)
    {
        vec3 n = normalize(grad);
        
        wo = sampleLambertian(n, uv);
        vec3 wm = normalize(wo + wi);
        pct = lambertian(col, wo, n);
        /*if (opacity > 0.5)
        {
            pct += vec3(EvaluateDisneyClearcoat(10.0, 1.0, wo, wm, wi, n)) * 10.0;
        }*/
    }
    else
    {
        wo = sampleSchlickPhase(wi, uv);
        pct = schlickPhase(col, wo, wi, -0.3);
    }

    imageStore(imgOutput, index, newCol);

    vec3 thpt = twoPi * pct;
    rayPosPk.xyz = ps;
    rayPosPk.w = acos(wo.z);
    imageStore(rayPosTex, index, rayPosPk);

    float phiOff = wo.x < 0.0 ? pi : 0.0;
    accumPk.rgb = accum * thpt; 
    accumPk.a = phiOff + atan(wo.y / wo.x);
    imageStore(accumTex, index, accumPk);

}