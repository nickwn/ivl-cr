
#version 430
#extension GL_ARB_shader_group_vote : require
#extension GL_ARB_shader_ballot : require
#extension GL_NV_shader_thread_shuffle : require // for shuffleXorNV

#pragma include("common.glsl")

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba16f, binding = 0) uniform image2D imgOutput;
layout(binding = 3) uniform sampler3D sigmaVolume;
layout(binding = 4) uniform samplerCube irrCubemap;
layout(rgba16f, binding = 5) uniform image2D rayPosTex;
layout(rgba16f, binding = 6) uniform image2D accumTex;
layout(binding = 7) uniform sampler1D clearcoatLUT; // TODO: replace with cubic function?
uniform uint numSamples;
uniform vec3 scaleFactor;
uniform vec3 lowerBound;
uniform int itrs;

// from Trevor Headstrom's code
vec2 rayBox(vec3 ro, vec3 rd, vec3 mn, vec3 mx) {
    vec3 id = 1 / rd;
    vec3 t0 = (mn - ro) * id;
    vec3 t1 = (mx - ro) * id;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    return vec2(max(max(tmin.x, tmin.y), tmin.z), min(min(tmax.x, tmax.y), tmax.z));
}

const float farT = 5.0; // hehe
const float stepSize = 0.001;
const float densityScale = 0.01;
const float lightingMult = 1.0;

// TODO: pack in floats? move out linearDensity?
struct CCJob
{
    vec3 linearDensity;
    uint steps;
    vec3 ro;
    float tStart;
    vec3 rd;
};

shared CCJob ccJobs[2 * 16 * 16];
shared uint numCCJobs;
shared uint numCCSteps;

void trace(in vec3 ro, in vec3 rd, in vec2 isect, in float diffuse, out vec3 transmittance)
{
    const float coneSpread = 0.325f;
    const float voxelSize = stepSize; // ??
    const float mipmapHardcap = 5.4f;

    isect.x = rand() * stepSize;

    // compute ray in index space

    uint firstJobIdx, lastJobIdx; // for use by clearcoat threads
    if (diffuse > .5f)
    {
        float multiplier = coneSpread / voxelSize;
        vec3 linearDensity = vec3(0.0f);
        while (isect.x < isect.y)
        {
            vec3 ps = ro + isect.x * rd;
            vec3 rel = ps;
            float l = multiplier * isect.x + stepSize;
            float level = log2(l);

            vec4 bakedVal = textureLod(sigmaVolume, rel, min(mipmapHardcap, level));
            vec3 sigmaT = vec3(pow(bakedVal.a, 1.5) * l) * (vec3(1.f) - bakedVal.rgb);

            linearDensity += sigmaT;
            isect.x += l;
        }

        transmittance = exp(vec3(-linearDensity * 10.f));
    }
    else
    {        
        const uint steps = uint(isect.y / stepSize) + 1;
        
        memoryBarrierShared(); // wait for job counters to be initialized by work group idx 0 (back in main)

        atomicAdd(numCCSteps, steps);

        memoryBarrierAtomicCounter();
        
        const uint threadsPerGroup = 16 * 16;
        const uint lwrStepsPerJob = numCCSteps / threadsPerGroup;
        const uint uprStepsPerJob = lwrStepsPerJob + ((numCCSteps % threadsPerGroup == 0) ? 0 : 1);
        const uint rayJobs = (steps / uprStepsPerJob) + ((steps % uprStepsPerJob == 0) ? 0 : 1);
        
        firstJobIdx = atomicAdd(numCCJobs, rayJobs); // reserve here
        lastJobIdx = firstJobIdx + rayJobs;
        const float jobTStep = float(uprStepsPerJob) * stepSize;

        float jobTItr = 0.f;
        uint stepItr = 0;
        for (uint jobItr = 0; jobItr < 256; jobItr++)
        {
            CCJob tempJob;
            tempJob.steps = 1; // min(uprStepsPerJob, steps - stepItr);
            tempJob.tStart = stepItr;
            tempJob.linearDensity = vec3(0.f);
            tempJob.ro = ro;
            tempJob.rd = rd;
            ccJobs[jobItr] = tempJob;

            jobTItr -= jobTStep;
            stepItr += uprStepsPerJob;
        }
    }

    //memoryBarrier();
    memoryBarrierShared(); // wait for job writes to be done
    memoryBarrierAtomicCounter();

    // execute jobs
    const uint jobIdx = gl_LocalInvocationIndex;
    if (numCCJobs > 300000)
    {
        CCJob job = ccJobs[jobIdx];
        vec2 tRange = vec2(job.tStart, job.tStart + float(job.steps) * stepSize);
        for (uint itr = 0; itr < job.steps; itr++)
        {
            vec3 uvw = job.ro + tRange.x * job.rd;

            vec4 bakedVal = textureLod(sigmaVolume, uvw, 0.f);
            vec3 sigmaT = vec3(pow(bakedVal.a, 1.5f) * stepSize) * (vec3(1.f) - bakedVal.rgb);

            job.linearDensity += sigmaT;
            tRange.x += stepSize;
        }

        ccJobs[jobIdx].linearDensity = job.linearDensity;
    }
    // TODO: repeat again?

    memoryBarrierShared(); // wait for jobs to finish

    // sum up job linear densities
    if (diffuse <= .5f)
    {
        vec3 linearDensity = vec3(0.f);
        for (uint jobItr = firstJobIdx; jobItr < lastJobIdx; jobItr++)
        {
            linearDensity += ccJobs[jobItr].linearDensity;
        }

        transmittance = exp(vec3(-linearDensity * 10.f));
    }
}

void main()
{
    // get index in global work group i.e x,y position
    ivec2 index = ivec2(gl_GlobalInvocationID.xy);
    if (gl_LocalInvocationIndex == 0)
    {
        numCCJobs = 0;
        numCCSteps = 0;
    }
    {
        // zero-initialize ccJobs
        CCJob tempJob;
        tempJob.steps = 0; // min(uprStepsPerJob, steps - stepItr);
        tempJob.tStart = 0.f;
        tempJob.linearDensity = vec3(0.f);
        tempJob.ro = vec3(0.f);
        tempJob.rd = vec3(0.f);
        //ccJobs[gl_LocalInvocationIndex] = tempJob;
    }

    ivec2 screenIndex = ivec2(index.x / numSamples, index.y);
    uint sampleNum = index.x % numSamples;

    vec4 accumPk = imageLoad(accumTex, index);
    vec3 accum = accumPk.rgb;

    if (length(accum) < 0.0001) return;

    // TODO: seed better
    initRNG(index, itrs);

    vec4 rayPosPk = imageLoad(rayPosTex, index);

    vec3 ro = rayPosPk.xyz;
    vec3 rd = vec3(
        sin(rayPosPk.w) * cos(accumPk.w),
        sin(rayPosPk.w) * sin(accumPk.w),
        cos(rayPosPk.w)
    );

    ro = clamp((ro - lowerBound) * scaleFactor, vec3(0.f), vec3(1.f));
    rd *= scaleFactor;

    vec2 isect = rayBox(ro, rd, vec3(0.f), vec3(1.f));
    isect.x = max(0, isect.x);
    //isect.y = min(isect.y, farT);

    // init tracing from camera
    //ro += rd * isect.x;
    //isect.y -= isect.x;

    vec4 lastImgVal = imageLoad(imgOutput, index);

    vec3 transmittance;
    float diffuse = (-1.f * sign(lastImgVal.a) + 1.f) * .5f;
    trace(ro, rd, isect, diffuse, transmittance);

    vec4 invItr = vec4(1.f / abs(lastImgVal.a));

    float cubemapLod = diffuse * 7.416f;
    vec3 incoming = textureLod(irrCubemap, rd, cubemapLod).rgb * transmittance * accum;
    vec4 newCol = lastImgVal * (1.f - invItr) + vec4(incoming, 1.f) * invItr;

    float add = lastImgVal.a < 0.f ? 1.f : 0.f;
    newCol.a = abs(lastImgVal.a) + add;

    imageStore(imgOutput, index, newCol);
}