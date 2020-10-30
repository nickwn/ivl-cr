#version 430

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(r16, binding = 1) readonly uniform image3D rawVolume;
layout(binding = 3) uniform sampler1D opacityLUT;
layout(binding = 4) writeonly uniform image3D bakedVolume;

void main()
{
	ivec3 index = ivec3(gl_GlobalInvocationID.xyz);
   
    float density = imageLoad(rawVolume, index).r;
    float opacity = texture(opacityLUT, density).r;

    imageStore(bakedVolume, index, vec4(density * opacity));
}