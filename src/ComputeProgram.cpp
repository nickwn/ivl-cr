#include "ComputeProgram.h"

#include <fstream>
#include <iostream>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

ComputeProgram::ComputeProgram(std::string filename, std::vector<std::string> uniforms)
{
	std::string buffer;
	std::ifstream file(filename, std::ios::in);

	if (!file.is_open())
	{
		throw std::runtime_error("cant open file");
	}

	file.seekg(0, std::ios::end);
	const size_t size = file.tellg();
	buffer.resize(size);
	file.seekg(0);
	file.read(&buffer[0], size);

	mShader = glCreateShader(GL_COMPUTE_SHADER);
	char const* bufPtr = buffer.c_str();
	glShaderSource(mShader, 1, &bufPtr, nullptr);
	glCompileShader(mShader);
	
	GLint result = GL_FALSE;
	int infoLogSz;
	glGetShaderiv(mShader, GL_COMPILE_STATUS, &result);
	glGetShaderiv(mShader, GL_INFO_LOG_LENGTH, &infoLogSz);
	if (infoLogSz > 0)
	{
		std::string errMsg;
		errMsg.resize(infoLogSz + 1);
		glGetShaderInfoLog(mShader, infoLogSz, nullptr, errMsg.data());
		std::cerr << errMsg;
		throw std::runtime_error("shader compilation error");
	}

	mProgram = glCreateProgram();
	glAttachShader(mProgram, mShader);
	glLinkProgram(mProgram);

	// Check the program.
	glGetProgramiv(mProgram, GL_LINK_STATUS, &result);
	glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &infoLogSz);
	if (infoLogSz > 0)
	{
		std::string errMsg;
		errMsg.resize(infoLogSz + 1);
		glGetProgramInfoLog(mProgram, infoLogSz, nullptr, errMsg.data());
		std::cerr << errMsg;
		throw std::runtime_error("shader link err");
	}

	for (const std::string& uniform : uniforms)
	{
		mUniformMap[uniform] = glGetUniformLocation(mProgram, uniform.c_str());
	}
}

ComputeProgram::~ComputeProgram()
{
	glDetachShader(mProgram, mShader);
	glDeleteShader(mShader);
	glDeleteProgram(mProgram);
}

void ComputeProgram::Use()
{
	glUseProgram(mProgram);
}

template<>
void ComputeProgram::UpdateUniform(std::string name, const GLuint value)
{
	glUniform1ui(mUniformMap[name], value);
}

template<>
void ComputeProgram::UpdateUniform(std::string name, const float value)
{
	glUniform1f(mUniformMap[name], value);
}

template<>
void ComputeProgram::UpdateUniform(std::string name, const glm::vec3 value)
{
	glUniform3fv(mUniformMap[name], 1, glm::value_ptr(value));
}

template<>
void ComputeProgram::UpdateUniform(std::string name, const glm::mat4 value)
{
	glUniformMatrix4fv(mUniformMap[name], 1, GL_FALSE, glm::value_ptr(value));
}

void ComputeProgram::Execute(GLuint x, GLuint y, GLuint z)
{
	glDispatchCompute(x, y, z);
}