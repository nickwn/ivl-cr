#include "DrawQuad.h"

#include <iostream>
#include <string>
#include <stdexcept>
#include <glm/glm.hpp>

#include "GLObjects.h"

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

	GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
	readCompileAttachShader("shaders/draw_quad.vert", vertShader, mProgram);

	GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
	readCompileAttachShader("shaders/draw_quad.frag", fragShader, mProgram);

	linkProgram(mProgram);
}

DrawQuad::~DrawQuad()
{
	glDeleteProgram(mProgram);
}

void DrawQuad::Execute(const UniqueTexture& texture)
{
	glUseProgram(mProgram);
	glBindImageTexture(
		0, texture.Get(), 0, false, 0, GL_READ_WRITE, GL_RGBA16F);
	glUniform1i(0, 0);
	glUniform1ui(glGetUniformLocation(mProgram, "samples"), mNumSamples);
	glBindVertexArray(mArray);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}