#pragma once

#include <string>

#include <gl/glew.h>
#include <glm/glm.hpp>

class Dicom
{
public:
	Dicom(std::string folder);

	GLuint GetTexture() { return mTexture; }
	glm::ivec3 GetScanSize() { return mDim; }

private:
	GLuint mTexture;
	glm::ivec3 mDim;
};