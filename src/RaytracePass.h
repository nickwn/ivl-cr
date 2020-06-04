#pragma once

#include <memory>

#include <gl/glew.h>
#include <glm/glm.hpp>

#include "GLObjects.h"
#include "Dicom.h"
#include "PiecewiseFunction.h"

class RaytracePass
{
public:
	RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom);

	void Execute(bool resample);

	const UniqueTexture& GetColorTexture() { return mDenoiseTexture; }

	void SetView(const glm::mat4& view) { mView = view; }

	void SetItrs(int itrs) { mItrs = itrs; }

private:
	ComputeProgram mRaytraceProgram;
	ComputeProgram mGenRaysProgram;
	ComputeProgram mResampleProgram;
	ComputeProgram mDenoiseProgram;
	glm::ivec2 mSize;
	uint32_t mNumSamples;
	std::weak_ptr<Dicom> mDicom;

	//pos: xyzw -> xyz-theta
	//accum: xyzw -> rgb-phi
	UniqueTexture mPosTexture;
	UniqueTexture mAccumTexture;
	UniqueTexture mColorTexture;
	//UniqueTexture mReservoirTexture;
	UniqueTexture mDenoiseTexture;

	glm::vec3 mScaleFactor;
	glm::vec3 mLowerBound;
	glm::mat4 mView;

	int mItrs;
};

