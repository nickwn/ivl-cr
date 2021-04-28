#pragma once

#include <string>

#include <gl/glew.h>
#include <glm/glm.hpp>

#include "GLObjects.h"

class Dicom
{
public:
	Dicom(std::string folder, glm::vec3 ppoint, glm::vec3 pnorm);
	const UniqueTexture& GetTexture() const { return mUniqueTexture; }
	UniqueTexture& GetTexture() { return mUniqueTexture; }
	const glm::ivec3& GetScanSize() const { return mDim; }
	const glm::vec3& GetPhysicalSize() const { return mPhysicalSize; }

private:
	UniqueTexture mUniqueTexture;
	glm::ivec3 mDim;
	glm::vec3 mPhysicalSize;
};