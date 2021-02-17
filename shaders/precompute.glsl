#version 430

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;
layout(r16, binding = 1) readonly uniform image3D rawVolume;
layout(binding = 2) uniform sampler2D transferLUT;
layout(binding = 3) uniform sampler2D opacityLUT;
layout(binding = 4) writeonly uniform image3D bakedVolume;

uniform ivec3 scanResolution;

const ivec3 bakeResolution = ivec3(128);

void main()
{
	ivec3 index = ivec3(gl_GlobalInvocationID.xyz);

    const ivec3 itrRange = max(scanResolution / bakeResolution, 1);
    const ivec3 itrStart = index * itrRange;
    const ivec3 itrEnd = min(itrStart + itrRange, scanResolution);

    ivec3 itr = itrStart;
    vec4 avgCol = vec4(0.f);
    for (; itr.z < itrEnd.z; itr.z++)
    {
        for (; itr.y < itrEnd.y; itr.y++)
        {
            for (; itr.x < itrEnd.x; itr.x++)
            {
                float density = imageLoad(rawVolume, itr).r;
                float opacity = texture(opacityLUT, vec2(density, 0.f)).r;
                vec3 color = texture(transferLUT, vec2(density, 0.f)).rgb;
                avgCol += vec4(color, opacity);
            }
        }
    }

    avgCol /= float(itrRange.x * itrRange.y * itrRange.z);

    imageStore(bakedVolume, index, avgCol);
}