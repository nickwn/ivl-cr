#include "GlObjects.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "stb_image.h"

Cubemap::Cubemap(const std::vector<std::string>& filenames)
{
    glBindTexture(GL_TEXTURE_CUBE_MAP, mUniqueTexture.Get());

    int width, height, nrChannels;
    uint8_t* data;
    for (GLuint i = 0; i < filenames.size(); i++)
    {
        data = stbi_load(filenames[i].c_str(), &width, &height, &nrChannels, 0);
        if (data)
        {
            glTexImage2D(
                GL_TEXTURE_CUBE_MAP_POSITIVE_X + i,
                0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data
            );
            stbi_image_free(data);
        }
        else
        {
            std::cerr << "error opening " << filenames[i];
            stbi_image_free(data);
            throw std::runtime_error("error opening file");
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glGenerateTextureMipmap(mUniqueTexture.Get());
}

std::string processIncludes(std::string source, const std::string& includeDir)
{
	const static std::regex include_pattern("^[ ]*#[ ]*pragma[ ]+include[ ]*\\([ ]*[\\\"<](.*)[\\\">][ ]*\\).*");

	std::istringstream in(source);
	std::string::const_iterator srcBegin = std::begin(source);

	std::string line;
	while (std::getline(in, line))
	{
		std::string::const_iterator srcEnd = std::next(srcBegin, line.size());

		std::smatch match;
		if (std::regex_match(srcBegin, srcEnd, match, include_pattern))
		{
			if (match.size() < 2) throw std::runtime_error("invalid include");

			const std::string filename = includeDir + match[1].str();
			std::ifstream file(filename, std::ios::binary);
			if (!file.is_open()) throw std::runtime_error("invalid include");

			std::string buffer;
			file.seekg(0, std::ios::end);
			const std::streampos size = file.tellg();
			buffer.resize(size);
			file.seekg(0);
			file.read(&buffer[0], size);

			buffer += "\n";

			size_t newBegin = std::distance(std::cbegin(source), srcBegin) + buffer.size();
			source.replace(srcBegin, srcEnd, buffer);
			srcBegin = std::next(std::begin(source), newBegin + 1);
		}

		else if (srcEnd != std::end(source)) srcBegin = srcEnd + 1;
	}

	return source;
}

void readCompileAttachShader(const std::string& filename, GLuint shader, GLuint program)
{
	std::string buffer;
	std::ifstream file(filename, std::ios::in);

	if (!file.is_open())
	{
		throw std::runtime_error("cant open file");
	}

	std::string includeDir;
	std::getline(std::istringstream(filename), includeDir, '/');
	includeDir += "/";

	file.seekg(0, std::ios::end);
	const size_t size = file.tellg();
	buffer.resize(size);
	file.seekg(0);
	file.read(&buffer[0], size);

	std::string fullSource = processIncludes(buffer, includeDir);

	char const* bufPtr = fullSource.c_str();
	glShaderSource(shader, 1, &bufPtr, nullptr);
	glCompileShader(shader);

	GLint result = GL_FALSE;
	int infoLogSz;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogSz);
	if (infoLogSz > 0)
	{
		std::string errMsg;
		errMsg.resize(infoLogSz + 1);
		glGetShaderInfoLog(shader, infoLogSz, nullptr, errMsg.data());
		std::cerr << errMsg;
		throw std::runtime_error("shader compilation error");
	}

	glAttachShader(program, shader);
}

void linkProgram(GLuint program)
{
	glLinkProgram(program);

	// Check the program.
	GLint result = GL_FALSE;
	int infoLogSz;
	glGetProgramiv(program, GL_LINK_STATUS, &result);
	glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogSz);
	if (infoLogSz > 0)
	{
		std::string errMsg;
		errMsg.resize(infoLogSz + 1);
		glGetProgramInfoLog(program, infoLogSz, nullptr, errMsg.data());
		std::cerr << errMsg;
		throw std::runtime_error("shader link err");
	}
}

ComputeProgram::ComputeProgram(std::string filename, std::vector<std::string> uniforms)
{	
	mShader = glCreateShader(GL_COMPUTE_SHADER);
	mProgram = glCreateProgram();
	readCompileAttachShader(filename, mShader, mProgram);
	linkProgram(mProgram);

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
void ComputeProgram::UpdateUniform(std::string name, const GLint value)
{
	glUniform1i(mUniformMap[name], value);
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