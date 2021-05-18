#include "RaytracePass.h"

#include <algorithm>

#include <glm/gtx/component_wise.hpp>

RaytracePass::RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom, GLuint transferLUT, GLuint opacityLUT)
	: mRaytraceProgram("shaders/raymarch.glsl", { "numSamples", "scaleFactor", "scanSize", "scanResolution", "lowerBound", "view", "itrs", "depth" }, 
		{ {"rawVolume", {GL_TEXTURE1, GL_TEXTURE_3D}}, {"transferLUT", {GL_TEXTURE2, GL_TEXTURE_2D}}, {"opacityLUT", {GL_TEXTURE3, GL_TEXTURE_1D}}, {"cubemap", {GL_TEXTURE4, GL_TEXTURE_CUBE_MAP}}, {"clearcoatLUT", {GL_TEXTURE7, GL_TEXTURE_2D}} },
		{ {"imgOutput", {0, GL_READ_WRITE, GL_RGBA16F}}, {"rayPosTex", {5, GL_READ_WRITE, GL_RGBA16F}}, {"accumTex", {6, GL_READ_WRITE, GL_RGBA16F}} })
	, mGenRaysProgram("shaders/gen_rays.glsl", { "numSamples", "view", "itrs" }, {},
		{ {"imgOutput", {0, GL_READ_WRITE, GL_RGBA16F}}, {"rayPosTex", {5, GL_READ_WRITE, GL_RGBA16F}}, {"accumTex", {6, GL_READ_WRITE, GL_RGBA16F}} })
	, mDenoiseProgram("shaders/denoise.glsl", {}) // TODO: add texture/image bindings
	, mPrecomputeProgram("shaders/precompute.glsl", { "scanResolution" }, 
		{ { "transferLUT", {GL_TEXTURE2, GL_TEXTURE_2D} }, { "opacityLUT", {GL_TEXTURE3, GL_TEXTURE_1D} } },
		{ {"rawVolume", {1, GL_READ_ONLY, GL_R16}} , {"bakedVolume", {4, GL_WRITE_ONLY, GL_RGBA16}} })
	, mConeTraceProgram("shaders/raymarch_direct.glsl", { "numSamples", "scaleFactor", "lowerBound", "itrs" }, 
		{ {"sigmaVolume", {GL_TEXTURE3, GL_TEXTURE_3D}}, {"cubemap", {GL_TEXTURE4, GL_TEXTURE_CUBE_MAP}}, {"clearcoatLUT", {GL_TEXTURE7, GL_TEXTURE_2D}} },
		{ {"imgOutput", {0, GL_READ_WRITE, GL_RGBA16F}}, {"rayPosTex", {5, GL_READ_WRITE, GL_RGBA16F}}, {"accumTex", {6, GL_READ_WRITE, GL_RGBA16F}} })
	, mSize(size)
	, mNumSamples(samples)
	, mDicom(dicom)
	, mPhysicalSize()
	, mItrs(1)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mDenoiseTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

	glBindTexture(GL_TEXTURE_2D, mColorTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(0, mColorTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	glBindTexture(GL_TEXTURE_2D, mPosTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(5, mPosTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	glBindTexture(GL_TEXTURE_2D, mAccumTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(6, mAccumTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	mView = glm::mat4(1.f);
	mLowerBound = glm::vec3(0.f);
	mScaleFactor = glm::vec3(0.f);

	glm::ivec3 dicomDim = mDicom.lock()->GetScanSize();
	GLint lodLevels = 1 + std::floor(std::log2(glm::compMax(dicomDim)));

	const glm::ivec3 bakeSize = glm::ivec3(128);
	glBindTexture(GL_TEXTURE_3D, mBakedVolumeTexture.Get());
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, bakeSize.x, bakeSize.y, bakeSize.z, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
	glGenerateTextureMipmap(mBakedVolumeTexture.Get()); // For some reason I have to do this twice or there is a crash later

	// create the baked volume texture containing (rgb transfer lut color, transfer lut opacity * density)
	mPrecomputeProgram.Use();
	mPrecomputeProgram.BindTexture("transferLUT", transferLUT);
	mPrecomputeProgram.BindTexture("opacityLUT", opacityLUT);
	mPrecomputeProgram.BindImage("rawVolume", dicom->GetTexture().Get(), 0);
	mPrecomputeProgram.BindImage("bakedVolume", mBakedVolumeTexture.Get(), 0);
	mPrecomputeProgram.UpdateUniform("scanResolution", dicom->GetScanSize());
	mPrecomputeProgram.Execute(bakeSize.x / 8, bakeSize.y / 8, bakeSize.z / 8);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	// generate mipmap for the volume texture generated in the previous compute shader
	glGenerateTextureMipmap(mBakedVolumeTexture.Get());
}

void RaytracePass::Execute(GLuint transferLUT, GLuint opacityLUT, GLuint clearcoatLUT, GLuint cubemap, GLuint volume)
{
	const glm::vec3 scanSize = glm::vec3(mDicom.lock()->GetScanSize());
	const glm::vec3 physicalSize = mPhysicalSize; // glm::vec3(mDicom.lock()->GetPhysicalSize());
	const float invMaxComp = 1.f / std::max(std::max(physicalSize.x, physicalSize.y), physicalSize.z);
	mLowerBound = -physicalSize * 0.5f;// invMaxComp;
	const glm::vec3 upperBound = physicalSize * 0.5f;// invMaxComp;
	const glm::vec3 boundDim = (upperBound - mLowerBound);
	mScaleFactor = 1.f / boundDim;

	// generate the camera rays
	mGenRaysProgram.Use();
	mGenRaysProgram.BindImage("imgOutput", mColorTexture.Get());
	mGenRaysProgram.BindImage("rayPosTex", mPosTexture.Get());
	mGenRaysProgram.BindImage("accumTex", mAccumTexture.Get());
	mGenRaysProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mGenRaysProgram.UpdateUniform("view", mView);
	mGenRaysProgram.UpdateUniform("itrs", mItrs);
	mGenRaysProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	// trace the camera rays
	mRaytraceProgram.Use();
	mRaytraceProgram.BindTexture("rawVolume", volume);
	mRaytraceProgram.BindTexture("transferLUT", transferLUT);
	mRaytraceProgram.BindTexture("opacityLUT", opacityLUT);
	mRaytraceProgram.BindTexture("cubemap", cubemap);
	mRaytraceProgram.BindTexture("clearcoatLUT", clearcoatLUT);
	mRaytraceProgram.BindImage("imgOutput", mColorTexture.Get());
	mRaytraceProgram.BindImage("rayPosTex", mPosTexture.Get());
	mRaytraceProgram.BindImage("accumTex", mAccumTexture.Get());
	mRaytraceProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mRaytraceProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mRaytraceProgram.UpdateUniform("scanSize", scanSize);
	mRaytraceProgram.UpdateUniform("scanResolution", glm::vec3(mDicom.lock()->GetScanSize()));
	mRaytraceProgram.UpdateUniform("lowerBound", mLowerBound);
	mRaytraceProgram.UpdateUniform("view", mView);
	mRaytraceProgram.UpdateUniform("itrs", mItrs);
	mRaytraceProgram.UpdateUniform("depth", GLuint(1));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);
	
	// trace the direct lighting rays
	mConeTraceProgram.Use();
	mConeTraceProgram.BindTexture("sigmaVolume", mBakedVolumeTexture.Get());
	mConeTraceProgram.BindTexture("cubemap", cubemap);
	mConeTraceProgram.BindTexture("clearcoatLUT", clearcoatLUT);
	mConeTraceProgram.BindImage("imgOutput", mColorTexture.Get());
	mConeTraceProgram.BindImage("rayPosTex", mPosTexture.Get());
	mConeTraceProgram.BindImage("accumTex", mAccumTexture.Get());
	mConeTraceProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mConeTraceProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mConeTraceProgram.UpdateUniform("lowerBound", mLowerBound);
	mConeTraceProgram.UpdateUniform("itrs", mItrs);
	mConeTraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
	
	mItrs++;
}