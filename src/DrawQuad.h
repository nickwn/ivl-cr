#pragma once

#include <gl/glew.h>
#include <glm/fwd.hpp>

#include "GLObjects.h"

class DrawQuad
{
public:
	DrawQuad(glm::ivec2 size, uint32_t samples);
	~DrawQuad();

	void Execute(const UniqueTexture& texture);

private:
	uint32_t mNumSamples;
	GLuint mArray;
	GLuint mProgram;
};

