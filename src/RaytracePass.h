#pragma once

#include <memory>

#include <gl/glew.h>
#include <glm/glm.hpp>

#include "ComputeProgram.h"
#include "Dicom.h"
#include "PiecewiseFunction.h"

class RaytracePass
{
public:
	RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom);

	void Execute();

	GLuint GetColorTexture() { return mColorTexture; }

	void SetView(const glm::mat4& view) { mView = view; }

	void SetItrs(int itrs) { mItrs = itrs; }

private:
	ComputeProgram mComputeProgram;
	glm::ivec2 mSize;
	uint32_t mNumSamples;
	std::weak_ptr<Dicom> mDicom;

	GLuint mPosTexture;
	GLuint mDirTexture;
	GLuint mColorTexture;

	glm::vec3 mScaleFactor;
	glm::vec3 mLowerBound;
	glm::mat4 mView;

	int mItrs;
};

