#version 430
#pragma include("common.glsl")

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(binding = 1) uniform sampler3D rawVolume;
layout(binding = 2) uniform sampler1D transferLUT;
layout(binding = 3) uniform sampler1D opacityLUT;
layout(rgba16f, binding = 8) uniform image3D bakedVolume;
layout(rgba16f, binding = 9) uniform image3D bakedGradient;
uniform vec3 scaleFactor;

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


void main()
{
	ivec3 index = ivec3(gl_GlobalInvocationID.xyz);

    vec3 tuv = vec3(index) * scaleFactor;
    vec3 grad = calcGradient(tuv);
    imageStore(bakedGradient, index, vec4(grad, 0.0));

    float density = imageLoad(rawVolume, index).r;
    float opacity = texture(opacityLUT, density).r;
    vec3 color = texture(transferLUT, density).rgb;

    imageStore(bakedVolume, index, vec4(color, opacity));
}