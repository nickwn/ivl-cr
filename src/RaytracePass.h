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
	RaytracePass(const glm::ivec2& size, const uint32_t samples, std::shared_ptr<Dicom> dicom, GLuint transferLUT, GLuint opacityLUT);

	void Execute(GLuint transferLUT, GLuint opacityLUT, GLuint clearcoatLUT, GLuint cubemap, GLuint volume);

	const UniqueTexture& GetColorTexture() { return mColorTexture; }

	void SetView(const glm::mat4& view) { mView = view; }
	void SetPhysicalSize(const glm::vec3& physicalSize) { mPhysicalSize = physicalSize; }

	void SetItrs(int itrs) { mItrs = itrs; }
	int GetItrs() const { return mItrs; }

private:
	ComputeProgram mRaytraceProgram;
	ComputeProgram mGenRaysProgram;
	ComputeProgram mDenoiseProgram;
	ComputeProgram mPrecomputeProgram;
	ComputeProgram mConeTraceProgram;
	glm::ivec2 mSize;
	uint32_t mNumSamples;
	std::weak_ptr<Dicom> mDicom;

	//pos: xyzw -> xyz-theta
	//accum: xyzw -> rgb-phi
	UniqueTexture mPosTexture;
	UniqueTexture mAccumTexture;
	UniqueTexture mColorTexture;
	UniqueTexture mDenoiseTexture;
	UniqueTexture mBakedVolumeTexture;

	glm::vec3 mPhysicalSize;
	glm::vec3 mScaleFactor;
	glm::vec3 mLowerBound;
	glm::mat4 mView;

	int mItrs;  
};

