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

	void Reset()
	{
		glDeleteTextures(1, &mTexture);
	}

private:
	GLuint mTexture;
};

class Cubemap
{
public:
	Cubemap(const std::vector<std::string>& filenames);

	const UniqueTexture& Unique() const { return mUniqueTexture; }
private:
	UniqueTexture mUniqueTexture;
};

void readCompileAttachShader(const std::string& filename, GLuint shader, GLuint program);
void linkProgram(GLuint program);

class ComputeProgram
{
public:

	// for images/textures
	struct TexBinding
	{
		GLenum binding;
		GLenum target;
	};

	struct ImgBinding
	{
		GLuint binding;
		GLenum access;
		GLenum format;
	};

	ComputeProgram(std::string filename, std::vector<std::string> uniforms = {}, std::unordered_map<std::string, TexBinding> texBindings = {}, 
		std::unordered_map<std::string, ImgBinding> imgBindings = {});
	~ComputeProgram();

	void Use();

	template<typename T>
	void UpdateUniform(std::string name, T value);

	void BindTexture(std::string name, GLuint tex);

	void BindImage(std::string name, GLuint tex, GLuint level = 0);

	void Execute(GLuint x, GLuint y, GLuint z);

private:
	std::unordered_map<std::string, GLuint> mUniformMap;
	std::unordered_map<std::string, TexBinding> mTexBindings;
	std::unordered_map<std::string, ImgBinding> mImgBindings;

	GLuint mShader;
	GLuint mProgram;
};