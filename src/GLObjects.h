#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include <gl/glew.h>

class UniqueTexture
{
public:
	UniqueTexture() { glGenTextures(1, &mTexture); }
	~UniqueTexture() { glDeleteTextures(1, &mTexture); }

	UniqueTexture(const UniqueTexture&) = delete;
	UniqueTexture& operator=(const UniqueTexture&) = delete;

	UniqueTexture(UniqueTexture&& other) noexcept
	{
		mTexture = other.mTexture;
		other.mTexture = 0;
	}

	UniqueTexture&& operator=(UniqueTexture&& other)
	{
		UniqueTexture temp = std::move(other);
		Swap(temp);
	}

	GLuint Get() const { return mTexture; }
	void Swap(UniqueTexture& other) 
	{
		GLuint tempTex = other.mTexture;
		other.mTexture = mTexture;
		mTexture = tempTex;
	}

private:
	GLuint mTexture;
};

class Cubemap
{
public:
	Cubemap(const std::vector<std::string>& filenames);
private:
	UniqueTexture mUniqueTexture;
};

void readCompileAttachShader(const std::string& filename, GLuint shader, GLuint program);
void linkProgram(GLuint program);

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