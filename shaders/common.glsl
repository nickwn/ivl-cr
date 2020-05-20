const float pi = 3.141592653589;
const float twoPi = pi * 2.0;
const float invPi = 1.0 / pi;
const float invFourPi = 1.0 / (4.0 * pi);
const float e = 2.718281828459045;

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