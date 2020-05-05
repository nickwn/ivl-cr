#pragma once

#include <string>

#include <gl/glew.h>
#include <glm/glm.hpp>

class Dicom
{
public:
	Dicom(std::string folder);

	GLuint GetTexture() { return mTexture; }
	const glm::ivec3& GetScanSize() const { return mDim; }
	const glm::vec3& GetPhysicalSize() const { return mPhysicalSize; }

private:
	GLuint mTexture;
	glm::ivec3 mDim;
	glm::vec3 mPhysicalSize;
};