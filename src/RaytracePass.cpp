#include "RaytracePass.h"

#include <algorithm>

RaytracePass::RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom)
	: mRaytraceProgram("shaders/raymarch.glsl", { "numSamples", "scaleFactor", "lowerBound", "view", "itrs", "depth" })
	, mGenRaysProgram("shaders/gen_rays.glsl", { "numSamples", "view", "itrs" })
	, mResampleProgram("shaders/resample.glsl", { "numSamples" })
	, mDenoiseProgram("shaders/denoise.glsl", {})
	, mSize(size)
	, mNumSamples(samples)
	, mDicom(dicom)
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
}

void RaytracePass::Execute(bool resample)
{
	const glm::vec3 scanSize = glm::vec3(mDicom.lock()->GetScanSize());
	const glm::vec3 physicalSize = glm::vec3(mDicom.lock()->GetPhysicalSize());
	const float invMaxComp = 1.f / std::max(std::max(physicalSize.x, physicalSize.y), physicalSize.z);
	mLowerBound = -physicalSize * invMaxComp;
	const glm::vec3 upperBound = physicalSize * invMaxComp;
	const glm::vec3 boundDim = (upperBound - mLowerBound);
	mScaleFactor = 1.f / boundDim;

	mGenRaysProgram.Use();
	mGenRaysProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mGenRaysProgram.UpdateUniform("view", mView);
	mGenRaysProgram.UpdateUniform("itrs", mItrs);
	mGenRaysProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	glBindImageTexture(0, mColorTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
	mRaytraceProgram.Use();
	mRaytraceProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mRaytraceProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mRaytraceProgram.UpdateUniform("lowerBound", mLowerBound);
	mRaytraceProgram.UpdateUniform("view", mView);
	mRaytraceProgram.UpdateUniform("itrs", mItrs);
	mRaytraceProgram.UpdateUniform("depth", GLuint(1));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);
	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	mRaytraceProgram.UpdateUniform("depth", GLuint(2));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	mRaytraceProgram.UpdateUniform("depth", GLuint(3));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	mRaytraceProgram.UpdateUniform("depth", GLuint(4));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	mRaytraceProgram.UpdateUniform("depth", GLuint(5));
	mRaytraceProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);

	//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	glBindImageTexture(6, mDenoiseTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
	mDenoiseProgram.Use();
	mDenoiseProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);
	mDenoiseProgram.Execute((mSize.x * mNumSamples) / 16, mSize.y / 16, 1);
	glBindImageTexture(6, mAccumTexture.Get(), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);

	mItrs++;
}