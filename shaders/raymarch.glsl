#version 430
layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba32f, binding = 0) uniform image2D imgOutput;
layout(binding = 1) uniform sampler3D rawVolume;
layout(binding = 2) uniform sampler1D transferLUT;
layout(binding = 3) uniform sampler1D opacityLUT;
layout(binding = 4) uniform samplerCube cubemap;
uniform uint numSamples;
uniform vec3 scaleFactor;
uniform vec3 lowerBound;
uniform mat4 view;
uniform int itrs;

float pi = 3.141592653589;
float e = 2.718281828459045;

uint state[4];

int hash(int x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint rotl(const uint x, int k) {
    return (x << k) | (x >> (32 - k));
}

// xoshiro128+
// from http://prng.di.unimi.it/xoshiro128plus.c
uint nextUInt() {
    const uint result = state[0] + state[3];

    const uint t = state[1] << 9;

    state[2] ^= state[0];
    state[3] ^= state[1];
    state[1] ^= state[2];
    state[0] ^= state[3];

    state[2] ^= t;

    state[3] = rotl(state[3], 11);

    return result;
}

// from https://github.com/wjakob/pcg32/blob/master/pcg32.h
float noise()
{
    /* Trick from MTGP: generate an uniformly distributed
           single precision number in [1,2) and subtract 1. */
    uint u = (nextUInt() >> 9) | 0x3f800000u;
    return uintBitsToFloat(u) - 1.0f;
}

// shitty 2d noise func
vec2 noise2()
{
    return vec2(noise(), noise());
}

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

// surface brdf function
vec3 lambertian(vec3 color, vec3 wo, vec3 n)
{
    return color * max(0.0, dot(wo, n)) / pi;
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
    uv = uv * 2 - 1;
    vec2 uv2 = uv * uv;
    float x = 2 * uv.x * sqrt(1 - uv2.x - uv2.y);
    float y = 2 * uv.y * sqrt(1 - uv2.x - uv2.y);
    float z = 1 - 2 * (uv2.x + uv2.y);
    return vec3(x, y, z);
}

// probability that brdf will be used vs phase function
float pBRDF(float opacity, float gradMag, float g)
{
    return opacity * (1 - pow(e, -25 * g * g * g * gradMag));
}

vec3 calcGradient(vec3 tuv)
{
    return -vec3( 
        textureOffset(rawVolume, tuv, ivec3(1, 0, 0)).r - textureOffset(rawVolume, tuv, ivec3(-1, 0, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, 1, 0)).r - textureOffset(rawVolume, tuv, ivec3(0, -1, 0)).r,
        textureOffset(rawVolume, tuv, ivec3(0, 0, 1)).r - textureOffset(rawVolume, tuv, ivec3(0, 0, -1)).r
    );
}

const vec2 screenRes = vec2(1280.0, 720.0); // todo: make uniform
const vec2 halfRes = screenRes * 0.5;
const float z = 1.0 / tan(radians(45.0) * 0.5);
const float farT = 5.0; // hehe

const float stepSize = 0.01;
const float maxAccum = 2.0;
const float densityScale = 0.01;

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);

    // TODO: seed better
    int seed1 = hash(index.x);
    int seed2 = hash(index.y);
    int seed3 = hash(itrs);
    state[0] = seed1 ^ seed3;
    state[1] = seed2 ^ seed3;
    state[2] = index.x ^ seed2;
    state[3] = index.y ^ seed1;
    //noise(); noise(); noise(); noise();

    vec2 clip = (vec2(screenIndex.xy + noise2()) - halfRes) / halfRes.y;
    vec3 rd = (view * vec4(normalize(vec3(clip, z)), 0.0)).xyz;
    vec3 ro = (view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    vec2 isect = rayBox(ro, rd, lowerBound, -1.0 * lowerBound);
    isect.x = max(0, isect.x);
    isect.y = min(isect.y, farT);

    // early out if no bb hit
    if (isect.x >= isect.y)
    {
        vec3 missCol = texture(cubemap, rd).rgb;
        imageStore(imgOutput, index, vec4(missCol, 1.0));
        return;
    }

    // init woodcock tracking from camera
    ro += rd * isect.x;
    isect.y -= isect.x;

    float s = -log(noise()) * densityScale;
    float sum = 0.0f;
    uint hit = 1;
    isect.x = noise() * stepSize;

    vec3 ps = vec3(0.0), rel = vec3(0.0);
    float density = 0.0;
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
        float opacity = texture(opacityLUT, density).r;

        float sigmaT = density * opacity;

        sum += sigmaT * stepSize;
        isect.x += stepSize;
    }

    if (hit==0)
    {
        vec3 missCol = texture(cubemap, rd).rgb;
        //imageStore(imgOutput, index, vec4(missCol, 1.0));
        float invItr = 1.0 / float(itrs);
        vec4 newCol = imageLoad(imgOutput, index) * (1.0 - invItr) + vec4(missCol, 1.0) * invItr;
        imageStore(imgOutput, index, newCol);
        return;
    }

    vec3 col = texture(transferLUT, density).rgb;

    // shade with brdf or phase function
    vec3 grad = calcGradient(rel);
    vec2 uv = noise2();
    vec3 wi = -rd, wo = vec3(0.0), pct = vec3(0.0);
    float pbrdf = pBRDF(density, length(grad), 1.0);
    if (noise() < 1.0)
    {
        vec3 n = normalize(grad);
        wo = sampleLambertian(n, uv);
        pct = lambertian(col, wo, n); //* vec3(1.0, 0.0, 0.0);
    }
    else
    {
        wo = sampleSchlickPhase(wi, uv);
        pct = schlickPhase(col, wo, wi, 0.0);// * vec3(0.0, 1.0, 0.0);
    }

    isect = rayBox(ps, wo, lowerBound, -1.0 * lowerBound);
    isect.y = min(isect.y, farT);
    //isect.y = max(0.0, max(isect.x, isect.y));

    // init woodcock tracking from light source
    s = -log(noise()) * densityScale;
    sum = 0.0f;
    hit = 1;
    isect.x = noise() * stepSize;

    ro = ps + isect.y * wo;
    rd = -wo;
    while (sum < s)
    {
        vec3 ps = ro + isect.x * rd;
        vec3 rel = (ps - lowerBound) * scaleFactor;

        if (isect.x >= isect.y)
        {
            hit = 0;
            break;
        }

        density = texture(rawVolume, rel).r;
        float opacity = texture(opacityLUT, density).r;

        float sigmaT = density * opacity;

        sum += sigmaT * stepSize;
        isect.x += stepSize;
    }

    vec3 incomingRadiance = texture(cubemap, wo).rgb * 10.0;
    col = pct * incomingRadiance * (1-hit); //* incomingRadiance * visible;
    //col = vec3(visible);

    // output to a specific pixel in the image
    float invItr = 1.0 / float(itrs);
    vec4 newCol = imageLoad(imgOutput, index) * (1.0 - invItr) + vec4(col, 1.0) * invItr;
    imageStore(imgOutput, index, newCol);
}