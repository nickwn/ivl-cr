#include "RaytracePass.h"

#include <algorithm>

RaytracePass::RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom)
    : mComputeProgram("raymarch.glsl", { "imgOutput", "rawVolume", "transferLUT", "opacityLUT", "cubemap",
		"numSamples", "scaleFactor", "lowerBound", "view" })
	, mSize(size)
	, mNumSamples(samples)
	, mDicom(dicom)
{
	mPosTexture = 0;
	mDirTexture = 0;

	glGenTextures(1, &mColorTexture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mColorTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size.x * samples, size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
	glBindImageTexture(0, mColorTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

	mView = glm::mat4(1.f);
	mLowerBound = glm::vec3(0.f);
	mScaleFactor = glm::vec3(0.f);
}

void RaytracePass::Execute()
{
	const glm::vec3 scanSize = glm::vec3(mDicom.lock()->GetScanSize());
	const float invMaxComp = 1.f / std::max(std::max(scanSize.x, scanSize.y), scanSize.z);
	mLowerBound = -scanSize * invMaxComp;
	const glm::vec3 upperBound = scanSize * invMaxComp;
	const glm::vec3 boundDim = (upperBound - mLowerBound);
	mScaleFactor = 1.f / boundDim;

	glBindImageTexture(0, mColorTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	mComputeProgram.Use();
	mComputeProgram.UpdateUniform("numSamples", GLuint(mNumSamples));
	mComputeProgram.UpdateUniform("scaleFactor", mScaleFactor);
	mComputeProgram.UpdateUniform("lowerBound", mLowerBound);
	mComputeProgram.UpdateUniform("view", mView);
	mComputeProgram.Execute((mSize.x * mNumSamples)/16, mSize.y/16, 1);
}