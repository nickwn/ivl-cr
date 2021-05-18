const float pi = 3.141592653589;
const float twoPi = pi * 2.0;
const float invPi = 1.0 / pi;
const float invFourPi = 1.0 / (4.0 * pi);
const float e = 2.718281828459045;

uint state[4];

uint hash(uint x) {
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

void initRNG(ivec2 index, uint itrs)
{
    uint rand = floatBitsToUint(fract(sin(dot(vec2(index), vec2(12.9898, 78.233))) * 43758.5453123));
    uint seed1 = hash(index.x) ^ rand;
    uint seed2 = hash(index.y) ^ rand;
    uint seed3 = hash(itrs);
    state[0] = seed1 ^ seed3;
    state[1] = seed2 ^ seed3;
    state[2] = index.x ^ seed2;
    state[3] = index.y ^ seed1;
    nextUInt(); nextUInt();
}

// from https://github.com/wjakob/pcg32/blob/master/pcg32.h
float rand()
{
    /* Trick from MTGP: generate an uniformly distributed
           single precision number in [1,2) and subtract 1. */
    uint u = (nextUInt() >> 9) | 0x3f800000u;
    return uintBitsToFloat(u) - 1.0f;
}

// shitty 2d rand func
vec2 rand2()
{
    return vec2(rand(), rand());
}

float getVoxelOpacity(uint maskMode, sampler1D opacityLUT, uint mask, float density)
{
    if (maskMode == 2 && mask == 0)
    {
        return 0.f;
    }

    return texture(opacityLUT, density).r;
}

// maskMode - 0:none, 1:body, 2:isolate
vec3 getVoxelColor(uint maskMode, sampler2D transferLUT, uint mask, float density)
{
    const uint organCount = 7;
    vec3 organColors[organCount] = vec3[](
        vec3(0.992, 0.968, 0.843), // bladder
        vec3(0, 0.278, 1),         // kidney
        vec3(0.752, 0.635, 0.996), // colon
        vec3(1, 0.874, 0.109),     // spleen
        vec3(0.968, 0.780, 1),     // illeum
        vec3(1, 0, 0),             // aorta
        vec3(0, .8, 1)             // vein
    );

    vec3 color;
    if (maskMode != 0)
    {
        if (mask != 0)
        {
            color = vec3(1.f);
            for (uint i = 0; i < organCount; i++)
            {
                if ((mask & (0x1 << i)) != 0)
                {
                    color *= organColors[i];
                }
            }
        }
        else
        {
            color = texture(transferLUT, vec2(density, 0.f)).rgb;
        }

        //opacity = min(1.f, opacity * 10.f); // artificially boost opacity if masked because they can be kind of hidden otherwise
    }
    else
    {
        color = texture(transferLUT, vec2(density, 0.f)).rgb;
    }

    return color;
}