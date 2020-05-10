#include "PiecewiseFunction.h"

void PiecewiseFunction<float, glm::vec4, LinearInterp<float, glm::vec4>>::GenTexture(const std::vector<glm::vec4>& evals) const
{
	glBindTexture(GL_TEXTURE_1D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, evals.size(), 0, GL_RGBA, GL_FLOAT, evals.data());
}

void PiecewiseFunction<float, float, ConstInterp<float, float>>::GenTexture(const std::vector<float>& evals) const
{
	glBindTexture(GL_TEXTURE_1D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // would be better if sampled lower, but high sample cound should solve
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, evals.size(), 0, GL_RED, GL_FLOAT, evals.data());
}

void PiecewiseFunction<float, float, LinearInterp<float, float>>::GenTexture(const std::vector<float>& evals) const
{
	glBindTexture(GL_TEXTURE_1D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // would be better if sampled lower, but high sample cound should solve
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, evals.size(), 0, GL_RED, GL_FLOAT, evals.data());
}