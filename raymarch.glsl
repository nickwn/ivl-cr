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

float pi = 3.141592653589;
float e = 2.718281828459045;

// Interleaved Gradient Noise by Jorge Jimenez.
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
float interleavedGradientNoise(vec2 xy)
{
    return fract(52.9829189f * fract(xy.x * 0.06711056f + xy.y * 0.00583715f));
}

float noise(vec2 xy)
{
    return interleavedGradientNoise(xy * 4000.0);
}

// shitty 2d noise func
vec2 noise2(vec2 xy)
{
    const float subdivs = 8196.0;
    const float subdivSz = 1.0 / subdivs;
    const float subdivSz2 = subdivSz * subdivSz;

    const float randNorm = interleavedGradientNoise(xy * 2000.0);
    return vec2(mod(randNorm, subdivSz) * subdivs, (randNorm * subdivs) * subdivSz); // ??
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
    float sign = sign(n.z);
    const float a = -1.0f / (sign + n.z);
    const float b = n.x * n.y * a;
    b1 = vec3(1.0f + sign * n.x * n.x * a, sign * b, -sign * n.x);
    b2 = vec3(b, sign + n.y * n.y * a, -n.y);
}

// surface brdf function
vec3 lambertian(vec3 color, vec3 wo, vec3 n)
{
    return color * max(0.0, dot(wo, n)) / pi;
    //return vec3(1.0) * max(0.0, dot(wo, n));
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

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);

    vec2 clip = (vec2(screenIndex.xy + noise2(index.xy)) - halfRes) / halfRes.y;
    vec3 rd = (view * vec4(normalize(vec3(clip, z)), 0.0)).xyz;
    vec3 ro = (view * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    vec2 isect = rayBox(ro, rd, lowerBound, -1.0 * lowerBound);
    isect.x = max(0, isect.x);
    isect.y = min(isect.y, farT);

    if (isect.x >= isect.y)
    {
        vec3 missCol = texture(cubemap, rd).rgb;
        imageStore(imgOutput, index, vec4(missCol, 1.0));
        return;
    }

    ro += rd * isect.x;
    isect.y -= isect.x;

    float samples = isect.y / stepSize;
    vec3 dp = rd * stepSize;

    vec3 p = ro;
    float density = 0.0, opacity = 0.0, accum = 0.0;
    vec3 rel = vec3(0.0);
    for (float t = stepSize; t <= isect.y; t += stepSize)
    {
        p += dp;
        rel = (p - lowerBound) * scaleFactor;
        density = texture(rawVolume, rel).r;
        opacity = texture(opacityLUT, density).r;

        accum += density * opacity; // TODO: actually make opacity TF an opacity TF instead of just visible/not
        if (accum >= noise(index.xy) * maxAccum) break;
        opacity = 0.0; // TODO: fix shitty hack
    }

    if (opacity == 0.0)
    {
        vec3 missCol = texture(cubemap, rd).rgb;
        imageStore(imgOutput, index, vec4(missCol, 1.0));
        return;
    }

    vec3 col = texture(transferLUT, density).rgb * opacity;

    vec3 grad = calcGradient(rel);
    vec2 uv = noise2(index.xy);
    vec3 wi = -rd, wo = vec3(0.0), pct = vec3(0.0);
    float pbrdf = pBRDF(density * opacity, length(grad), 1.0);
    if (noise(index.xy) < 1.0)
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

    isect = rayBox(p, wo, lowerBound, -1.0 * lowerBound);
    isect.y = min(isect.y, farT);
    //isect.y = max(0.0, max(isect.x, isect.y));
    p = p + wo * isect.y;
    dp = -wo * stepSize;
    accum = 0.0;
    uint visible = 1;
    for (float t = stepSize; t <= isect.y; t += stepSize)
    {
        p += dp;
        rel = (p - lowerBound) * scaleFactor;
        density = texture(rawVolume, rel).r;
        opacity = texture(opacityLUT, 1 - density).r;

        accum += density * opacity;
        if (accum >= noise(index.xy) * maxAccum * 4)
        {
            visible = 0;
            break;
        }
    }

    vec3 incomingRadiance = texture(cubemap, wo).rgb * 10.0;
    col = pct * incomingRadiance * visible; //* incomingRadiance * visible;
    //col = vec3(visible);

    // output to a specific pixel in the image
    imageStore(imgOutput, index, vec4(col, 1.0));
}