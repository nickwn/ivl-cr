#include "RaytracePass.h"

#include <algorithm>

#include <glm/gtx/component_wise.hpp>

RaytracePass::RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom)
	: mRaytraceProgram("shaders/raymarch.glsl", { "numSamples", "scaleFactor", "scanSize", "scanResolution", "lowerBound", "view", "itrs", "depth" })
	, mGenRaysProgram("shaders/gen_rays.glsl", { "numSamples", "view", "itrs" })
	, mDenoiseProgram("shaders/denoise.glsl", {})
	, mPrecomputeProgram("shaders/precompute.glsl", { "scanResolution" })
	, mConeTraceProgram("shaders/raymarch_direct.glsl", { "numSamples", "scaleFactor", "lowerBound", "itrs" })
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mColorTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(0, mColorTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	glActiveTexture(GL_TEXTURE5);
	glBindTexture(GL_TEXTURE_2D, mPosTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(5, mPosTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_2D, mAccumTexture.Get());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x * samples, size.y, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
	glBindImageTexture(6, mAccumTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	mView = glm::mat4(1.f);
	mLowerBound = glm::vec3(0.f);
	mScaleFactor = glm::vec3(0.f);

	glm::ivec3 dicomDim = mDicom.lock()->GetScanSize();
	GLint lodLevels = 1 + std::floor(std::log2(glm::compMax(dicomDim)));

	const glm::ivec3 bakeSize = glm::ivec3(128);
	glActiveTexture(GL_TEXTURE4);
	glBindTexture(GL_TEXTURE_3D, mBakedVolumeTexture.Get());
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
	//glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, dicom->GetScanSize().x, dicom->GetScanSize().y, dicom->GetScanSize().z, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16, bakeSize.x, bakeSize.y, bakeSize.z, 0, GL_RGBA, GL_UNSIGNED_SHORT, nullptr);
	glGenerateTextureMipmap(mBakedVolumeTexture.Get()); // For some reason I have to do this twice or there is a crash later

	// create the baked volume texture containing (rgb transfer lut color, transfer lut opacity * density)
	glBindImageTexture(1, dicom->GetTexture().Get(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_R16);
	glBindImageTexture(4, mBakedVolumeTexture.Get(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16);
	mPrecomputeProgram.Use();
	mPrecomputeProgram.UpdateUniform("scanResolution", dicom->GetScanSize());
	mPrecomputeProgram.Execute(bakeSize.x / 8, bakeSize.y / 8, bakeSize.z / 8);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

	// generate mipmap for the volume texture generated in the previous compute shader
	glGenerateTextureMipmap(mBakedVolumeTexture.Get());
}

void RaytracePass::Execute(GLuint opacityLUT)
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
	mGenRaysProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mGenRaysProgram.UpdateUniform("view", mView);
	mGenRaysProgram.UpdateUniform("itrs", mItrs);
	mGenRaysProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glBindImageTexture(3, mBakedVolumeTexture.Get(), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16);
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_1D, opacityLUT);

	// trace the camera rays
	mRaytraceProgram.Use();
	mRaytraceProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mRaytraceProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mRaytraceProgram.UpdateUniform("scanSize", scanSize);
	mRaytraceProgram.UpdateUniform("scanResolution", glm::vec3(mDicom.lock()->GetScanSize()));
	mRaytraceProgram.UpdateUniform("lowerBound", mLowerBound);
	mRaytraceProgram.UpdateUniform("view", mView);
	mRaytraceProgram.UpdateUniform("itrs", mItrs);
	mRaytraceProgram.UpdateUniform("depth", GLuint(1));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);
	
	glActiveTexture(GL_TEXTURE3);
	glBindTexture(GL_TEXTURE_3D, mBakedVolumeTexture.Get());

	// trace the direct lighting rays
	mConeTraceProgram.Use();
	mConeTraceProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mConeTraceProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mConeTraceProgram.UpdateUniform("lowerBound", mLowerBound);
	mConeTraceProgram.UpdateUniform("itrs", mItrs);
	mConeTraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
	
	mItrs++;
}