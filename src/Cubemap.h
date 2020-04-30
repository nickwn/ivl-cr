#pragma once

#include <string>
#include <vector>

#include <gl/glew.h>

class Cubemap
{
public:
	Cubemap(const std::vector<std::string>& filenames);

private:
	GLuint mTexture;
};