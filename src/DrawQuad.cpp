#include "DrawQuad.h"

#include <iostream>
#include <string>
#include <stdexcept>
#include <glm/glm.hpp>

void attachShader(GLuint mProgram, GLenum type, const char* code) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &code, NULL);
	glCompileShader(shader);
	glAttachShader(mProgram, shader);
	glDeleteShader(shader);
}

DrawQuad::DrawQuad(glm::ivec2 size, uint32_t samples)
	: mNumSamples(samples)
{
	GLfloat data[8] = {
	  -1,-1, -1, 1,
	   1,-1,  1, 1,
	};
	GLuint buffer;
	glCreateBuffers(1, &buffer);
	glNamedBufferStorage(buffer, sizeof(data), data, 0);

	int bufferIndex = 0;
	glCreateVertexArrays(1, &mArray);
	glVertexArrayVertexBuffer(
		mArray, bufferIndex, buffer, 0, sizeof(GLfloat) * 2);

	int positionAttrib = 0;
	glEnableVertexArrayAttrib(mArray, positionAttrib);
	glVertexArrayAttribFormat(
		mArray, positionAttrib, 2, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(
		mArray, positionAttrib, bufferIndex);

	mProgram = glCreateProgram();
	glObjectLabel(GL_PROGRAM, mProgram, -1, "TextureCopy");

	attachShader(mProgram, GL_VERTEX_SHADER, R"(
	  #version 450 core
	  layout(location=0) in vec2 coord;
	  void main(void) {
		gl_Position = vec4(coord, 0.0, 1.0);
	  }
	)");

	attachShader(mProgram, GL_FRAGMENT_SHADER, R"(
	  #version 450 core
	  readonly restrict uniform layout(rgba32f) image2D image;
	  uniform uint samples;
	  layout(location=0) out vec4 color;
	  void main(void) {
		vec3 col3 = vec3(0.0);
		for(uint i = 0; i < samples; i++)
		{
			col3 += imageLoad(image, ivec2(gl_FragCoord.xy) * ivec2(samples, 1) + ivec2(i, 0)).xyz;
		}
		color = vec4(col3/samples, 1.0);
	  }
	)");

	glLinkProgram(mProgram);
	GLint result;
	GLint infoLogSz;
	glGetProgramiv(mProgram, GL_LINK_STATUS, &result);
	glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &infoLogSz);
	if (result != GL_TRUE) {
		std::string errMsg;
		errMsg.resize(infoLogSz + 1);
		glGetProgramInfoLog(mProgram, infoLogSz, nullptr, errMsg.data());
		std::cerr << errMsg;
		throw std::runtime_error(errMsg);
	}
}

DrawQuad::~DrawQuad()
{
	glDeleteProgram(mProgram);
}

void DrawQuad::Execute(GLuint texture)
{
	glUseProgram(mProgram);
	glBindImageTexture(
		0, texture, 0, false, 0, GL_READ_WRITE, GL_RGBA32F);
	glUniform1i(0, 0);
	glUniform1ui(1, mNumSamples);
	glBindVertexArray(mArray);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}