#version 430
#pragma include("common.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
uniform uint numSamples;
uniform mat4 view;
uniform int itrs;

const vec2 screenRes = vec2(1920.0, 1080.0); // todo: make uniform
//const vec2 screenRes = vec2(3840.0, 2160.0);
const vec2 halfRes = screenRes * 0.5;
const float z = 1.0 / tan(radians(45.0) * 0.5);

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);

    // TODO: seed better
    initRNG(index, itrs);

    vec2 clip = (vec2(screenIndex.xy + rand2()) - halfRes) / halfRes.y;
    vec3 rd = (view * vec4(normalize(vec3(clip, z)), 0.0)).xyz;
    vec3 ro = (view * vec4(vec3(0.0), 1.0)).xyz;

    float phiOff = rd.x < 0.0 ? pi : 0.0;
    vec4 rayPosPk = vec4(ro, acos(rd.z)); // xyz-theta
    vec4 accumPk = vec4(vec3(1.0), phiOff + atan(rd.y / rd.x)); // rgb-phi

    if(itrs == 1)
    {
        vec4 lastImgVal = imageLoad(imgOutput, index);
        imageStore(imgOutput, index, vec4(lastImgVal.rgb, 1.0)); 
    }

    imageStore(rayPosTex, index, rayPosPk);
    imageStore(accumTex, index, accumPk);
}