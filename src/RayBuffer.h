#pragma once

#include <cstdint>

#include <gl/glew.h>
#include <glm/fwd.hpp>

class RayBuffer
{
public:
	RayBuffer(const glm::ivec2& size, const uint32_t samples);

	GLuint GetColorTexture() { return mColorTexture; }
private:
	GLuint mPosTexture;
	GLuint mDirTexture;
	GLuint mColorTexture;
};

