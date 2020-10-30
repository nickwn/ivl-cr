#define GLM_FORCE_SWIZZLE
#include "PiecewiseFunction.h"

#include <glm/gtx/color_space.hpp>

void PiecewiseFunction<float, glm::vec4, LinearInterp<float, glm::vec4>>::GenTexture(const std::vector<glm::vec4>& evals) const
{
	glBindTexture(GL_TEXTURE_1D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, evals.size(), 0, GL_RGBA, GL_FLOAT, evals.data());
}

void PiecewiseFunction<float, glm::vec4, ConstInterp<float, glm::vec4>>::GenTexture(const std::vector<glm::vec4>& evals) const
{
	glBindTexture(GL_TEXTURE_1D, mUniqueTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
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
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, evals.size(), 0, GL_RED, GL_FLOAT, evals.data());
}

void HSVTransferFunction::EvaluateTexture(const uint32_t size) const
{
	const float step = 1.f / float(size);

	const float contrastBottom = mContrast[0], contrastTop = mContrast[1], value = mContrast[2];
	// ported from calvr
	std::vector<glm::vec4> colors(size, glm::vec4(0.f, 1.f, value, 1.f));
	float d = 0.f;
	for (size_t i = 0; i < size; i++, d += step)
	{
		float hue = d;
		if (hue < contrastBottom)
		{
			hue = 0.f;
		}
		if (hue > contrastTop)
		{
			hue = 1.f;
		}

		hue = glm::smoothstep(contrastBottom, contrastTop, hue);
		hue = std::clamp(hue, 0.f, 1.f);
		colors[i].r = hue * 360.f;

		colors[i].rgb = glm::rgbColor(glm::vec3(colors[i]));
	}

	glBindTexture(GL_TEXTURE_1D, mUniqueColorTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, colors.size(), 0, GL_RGBA, GL_FLOAT, colors.data());
}

void OpacityTransferFunction::EvaluateTexture(const uint32_t size) const
{
	const float step = 1.f / float(size);

	// ported from calvr
	std::vector<float> highestOpacities(size, 0.f);
	float d = 0.f;
	for (size_t i = 0; i < size; i++, d += step)
	{
		for (const std::array<float, 5> & triangle : mOpacityTriangles)
		{
			// triangle format: 
			// 0      , 1     , 2           , 3        , 4
			// overall, lowest, bottom width, top width, center
			float a = glm::smoothstep((triangle[4] - triangle[3]) - (triangle[2] - triangle[3]), (triangle[4] - triangle[3]), d);
			if (a == 1.f)
			{
				a = 1.f - glm::smoothstep(triangle[4] + triangle[3], triangle[4] + triangle[3] + (triangle[2] - triangle[3]), a);
			}

			if (a != 0.0)
			{
				a *= 1.f / std::pow(2.f, (1.f - triangle[0]) * 10.f);
				const float lowestLimit = 1.f / std::pow(2.f, (1.f - triangle[1]) * 10.f);
				a = std::max(a, lowestLimit);
				if (a >= highestOpacities[i])
				{
					highestOpacities[i] = a;
				}
			}
		}
	}

	glBindTexture(GL_TEXTURE_1D, mUniqueOpacityTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, highestOpacities.size(), 0, GL_RED, GL_FLOAT, highestOpacities.data());
}


void SimpleTransferFunction::EvaluateOpacityTexture(const uint32_t size) const
{
	const float step = 1.f / float(size);

	// ported from calvr
	std::vector<float> highestOpacities(size, 0.f);
	float d = 0.f;
	for (size_t i = 0; i < size; i++, d += step)
	{
		for (const std::array<float, 5> & triangle : mOpacityTriangles)
		{
			// triangle format: 
			// 0      , 1     , 2           , 3        , 4
			// overall, lowest, bottom width, top width, center
			float a = glm::smoothstep((triangle[4] - triangle[3]) - (triangle[2] - triangle[3]), (triangle[4] - triangle[3]), d);
			if (a == 1.f)
			{
				a = 1.f - glm::smoothstep(triangle[4] + triangle[3], triangle[4] + triangle[3] + (triangle[2] - triangle[3]), a);
			}

			if (a != 0.0)
			{
				a *= 1.f / std::pow(2.f, (1.f - triangle[0]) * 10.f);
				const float lowestLimit = 1.f / std::pow(2.f, (1.f - triangle[1]) * 10.f);
				a = std::max(a, lowestLimit);
				if (a >= highestOpacities[i])
				{
					highestOpacities[i] = a;
				}
			}
		}
	}

	glBindTexture(GL_TEXTURE_1D, mUniqueOpacityTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, highestOpacities.size(), 0, GL_RED, GL_FLOAT, highestOpacities.data());
}

void SimpleTransferFunction::EvaluateColorTexture(const uint32_t size) const
{
	const float step = 1.f / float(size);

	const float contrastBottom = mContrast[0], contrastTop = mContrast[1], value = mContrast[2];
	// ported from calvr
	std::vector<glm::vec4> colors(size, glm::vec4(0.f, 1.f, value, 1.f));
	float d = 0.f;
	for (size_t i = 0; i < size; i++, d += step)
	{
		float hue = d;
		if (hue < contrastBottom)
		{
			hue = 0.f;
		}
		if (hue > contrastTop)
		{
			hue = 1.f;
		}

		hue = glm::smoothstep(contrastBottom, contrastTop, hue);
		hue = std::clamp(hue, 0.f, 1.f);
		colors[i].r = hue * 360.f;

		colors[i].rgb = glm::rgbColor(glm::vec3(colors[i]));
	}

	glBindTexture(GL_TEXTURE_1D, mUniqueColorTexture.Get());
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, colors.size(), 0, GL_RGBA, GL_FLOAT, colors.data());
}
