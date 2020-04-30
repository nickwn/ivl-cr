#pragma once

#include <gl/glew.h>
#include <glm/fwd.hpp>

class DrawQuad
{
public:
	DrawQuad(glm::ivec2 size, uint32_t samples);
	~DrawQuad();

	void Execute(GLuint texture);

private:
	uint32_t mNumSamples;
	GLuint mArray;
	GLuint mProgram;
};

