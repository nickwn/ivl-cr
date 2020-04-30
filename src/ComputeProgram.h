#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <gl/glew.h>
#include <glm/fwd.hpp>

class ComputeProgram
{
public:
	ComputeProgram(std::string filename, std::vector<std::string> uniforms = {});
	~ComputeProgram();

	void Use();

	template<typename T>
	void UpdateUniform(std::string name, T value);

	void Execute(GLuint x, GLuint y, GLuint z);

private:
	std::unordered_map<std::string, GLuint> mUniformMap;

	GLuint mShader;
	GLuint mProgram;
};