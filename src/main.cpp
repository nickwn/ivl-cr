
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS // yaml-cpp gives deprecation warnings with c++17 compiler

#include <string>
#include <iostream>
#include <memory>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <variant>
#include <chrono>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <yaml-cpp/yaml.h>

#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "Dicom.h"
#include "Window.h"
#include "RaytracePass.h"
#include "DrawQuad.h"
#include "PiecewiseFunction.h"
#include "GLObjects.h"

// Injecting a conversion template specialization for glm stuffs for YAML parsing
namespace YAML {
	template<>
	struct convert<glm::vec3> 
	{
		static Node encode(const glm::vec3& rhs) 
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs) 
		{
			if (!node.IsSequence() || node.size() != 3) 
			{
				return false;
			}

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::mat4> 
	{
		static constexpr glm::length_t matSz = glm::mat4::length() * glm::mat4::length();
		static Node encode(const glm::mat4& rhs) 
		{
			Node node;
			for (glm::length_t i = 0; i < matSz; i++)
			{
				const glm::length_t rowIdx = i / glm::mat4::length();
				const glm::length_t colIdx = i % glm::mat4::length();
				node.push_back(rhs[rowIdx][colIdx]);
			}
			return node;
		}

		static bool decode(const Node& node, glm::mat4& rhs) 
		{
			if (!node.IsSequence() || node.size() != 16) 
			{
				return false;
			}

			for (glm::length_t i = 0; i < matSz; i++)
			{
				const glm::length_t rowIdx = i / glm::mat4::length();
				const glm::length_t colIdx = i % glm::mat4::length();
				rhs[rowIdx][colIdx] = node[i].as<float>();
			}

			return true;
		}
	};

	template<>
	struct convert<glm::quat> 
	{
		static Node encode(const glm::quat& quat) 
		{
			Node node;
			node.push_back(quat.x);
			node.push_back(quat.y);
			node.push_back(quat.z);
			node.push_back(quat.w);
			return node;
		}

		static bool decode(const Node& node, glm::quat& rhs) 
		{
			if (!node.IsSequence() || node.size() != 3)
			{
				return false;
			}

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			rhs.w = node[3].as<float>();
			return true;
		}
	};
}

class ViewController : public MouseListener
{
public:
	ViewController(const glm::mat4& initial = glm::mat4(1.f))
		: mLastCachedView(initial)
		, mDragStartPos()
		, mCurrDelta()
		, mViewDirtied(false)
		, mResample(true)
	{}

	void HandleMotion(std::shared_ptr<Window> window, const glm::vec2& pos) override
	{
		if (mDragStartPos)
		{
			mCurrDelta = CalcRot(window);
		}
	}

	void HandleButton(std::shared_ptr<Window> window, int button, int action, int mods) override
	{
		if (button == GLFW_MOUSE_BUTTON_LEFT)
		{
			if (action == GLFW_PRESS)
			{
				mDragStartPos = window->GetMousePos();
				mCurrDelta = glm::mat4(1.f);
				mViewDirtied = true;
			}
			else if (action == GLFW_RELEASE)
			{
				mDragStartPos.reset();
				mCurrDelta.reset();
				mViewDirtied = false;
				mLastCachedView = *mCurrDelta * mLastCachedView;
			}
		}
		else if (button == GLFW_MOUSE_BUTTON_RIGHT)
		{
			if (action == GLFW_PRESS)
			{
				mResample = !mResample;
			}
		}
	}

	void HandleScroll(std::shared_ptr<Window> window, const glm::dvec2& offset)
	{
		const float scaleFactor = 1.f + std::clamp(float(offset.y * .1f), -.9f, .9f);

		mLastCachedView[3][0] *= scaleFactor;
		mLastCachedView[3][1] *= scaleFactor;
		mLastCachedView[3][2] *= scaleFactor;
		mViewDirtied = true;
	}

	glm::mat4 GetView() const
	{
		if (mDragStartPos)
		{
			return *mCurrDelta * mLastCachedView;
		}
		return mLastCachedView;
	}

	bool GetShouldResample() const
	{
		return mResample;
	}

	bool GetIsViewDirtied() const
	{
		bool temp = mViewDirtied;
		//mViewDirtied = false;
		return temp;
	}

private:
	inline glm::mat4 CalcRot(std::shared_ptr<Window> window)
	{
		const static glm::vec2 scaleFactor = glm::vec2(.5f, .5f);
		const glm::vec2 delta = window->GetMousePos() - *mDragStartPos;
		const glm::vec2 rotDelta = glm::radians(delta * scaleFactor);
		const glm::mat4 azRot = glm::rotate(-rotDelta.x, glm::vec3(0.f, 1.f, 0.f));
		const glm::mat4 altRot = glm::rotate(-rotDelta.y, glm::vec3(1.f, 0.f, 0.f));
		return azRot * altRot;
	}

	std::optional<glm::vec2> mDragStartPos;
	std::optional<glm::mat4> mCurrDelta;
	glm::mat4 mLastCachedView;
	bool mViewDirtied;
	bool mResample;
};

struct ImageWriter
{
	std::string mFolder;
	mutable uint32_t mProposedImageNumber;
	std::string mNextOutputImagePath;

	ImageWriter(std::string folder) :
		mFolder(std::move(folder)),
		mProposedImageNumber(0),
		mNextOutputImagePath()
	{
		if (!std::filesystem::exists(mFolder))
		{
			std::cerr << "folder " + mFolder + " not found";
			throw std::runtime_error("folder " + mFolder + " not found");
		}

		mNextOutputImagePath = GetNextFilename();
	}

	std::string GetNextFilename() const
	{
		const std::string imagePrefix = "cr";
		const std::string imageFiletype = ".png";
		std::string proposedFilename = imagePrefix + std::to_string(mProposedImageNumber) + imageFiletype;

		bool filenameChanged = false;
		do
		{
			filenameChanged = false;
			for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(mFolder))
			{
				if (entry.path().has_filename() && entry.path().filename().string() == proposedFilename)
				{
					mProposedImageNumber++;
					proposedFilename = imagePrefix + std::to_string(mProposedImageNumber) + imageFiletype;
					filenameChanged = true;
					break;
				}
			}
		} while (filenameChanged);

		return mFolder + proposedFilename;
	}

	void WriteImage(const Window* window)
	{
		if (mNextOutputImagePath.empty())
		{
			mNextOutputImagePath = GetNextFilename();
		}

		std::cout << "writing image to " << mNextOutputImagePath << "\n";

		glm::ivec2 fbSize = window->GetFramebufferSize();

		std::vector<std::array<GLubyte, 3>> pixels(fbSize.x * fbSize.y); // store as std::array<GLubyte, 3> so reversing later doesn't also reverse the channels
		glReadPixels(0, 0, fbSize.x, fbSize.y, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

		std::reverse(pixels.begin(), pixels.end()); // image is loaded in upside-down so...

		stbi_write_png(mNextOutputImagePath.c_str(), fbSize.x, fbSize.y, 3, pixels.data(), fbSize.x * 3);

		mNextOutputImagePath = std::string();
	}
};

void GLAPIENTRY MessageCallback(GLenum source,
	GLenum type,
	GLuint id,
	GLenum severity,
	GLsizei length,
	const GLchar* message,
	const void* userParam)
{
	fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
		(type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
		type, severity, message);

	if (type == GL_DEBUG_TYPE_ERROR)
	{
		__debugbreak();
	}
}

int main(int argc, char* argv[])
{
	static const std::string configsDir = "configs/";
	static const std::string scansDir = "scans/";

	// defaults
	std::string configFilename = configsDir + "config3.yaml";
	std::string scanFolder = scansDir + "Larry_2017/";

	if (argc > 1)
	{
		scanFolder = scansDir + argv[1] + "/";

		if (argc == 2)
		{
			configFilename = configsDir + argv[2];
		}
	}

	YAML::Node config = YAML::LoadFile(configFilename);

	// Creating window also intializes OpenGL context
	glm::ivec2 size = glm::ivec2(1920, 1080);
	//glm::ivec2 size = glm::ivec2(3840, 2160);
	std::shared_ptr<Window> win = std::make_shared<Window>(size, "Cinematic Renderer");

	// During init, enable debug output
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(MessageCallback, 0);

	// Volume matrix
	YAML::Node volumeNode = config["volume"];
	const glm::vec3 volumeTranslation = volumeNode["pos"].as<glm::vec3>();
	const glm::vec3 volumeScale = volumeNode["scale"].as<glm::vec3>();
	const glm::mat4 volumeRotation = volumeNode["rotation"].as<glm::mat4>(
		glm::mat4_cast(volumeNode["rotation"].as<glm::quat>(glm::quat()))
	);
	const glm::mat4 volumeMat = volumeRotation * glm::translate(volumeTranslation);

	// Camera matrix
	YAML::Node cameraNode = config["camera"];
	const glm::mat4 cameraMat = glm::lookAt(cameraNode["pos"].as<glm::vec3>(), cameraNode["center"].as<glm::vec3>(), cameraNode["up"].as<glm::vec3>() * 1.f);

	const glm::mat4 initialView = volumeMat * cameraMat;
	std::shared_ptr<ViewController> viewController = std::make_shared<ViewController>(initialView);
	win->AddMouseListener(viewController);

	// Context is created, so now we can init textures
	YAML::Node transferFunction = config["transfer function"];

	YAML::Node opacityTrianglesNode = transferFunction["opacity"];
	std::vector<std::array<float, 5>> opacityTriangles;
	for (YAML::const_iterator it = opacityTrianglesNode.begin(); it != opacityTrianglesNode.end(); it++)
	{
		std::array<float, 5> triangle = it->as<std::array<float, 5>>();
		opacityTriangles.push_back(triangle);
	}

	OpacityTransferFunction opacityTF = OpacityTransferFunction(opacityTriangles);
	/*PLF<float, float> opacityTF = PLF<float, float>();
	opacityTF.AddStop(0.f, 0.f);
	opacityTF.AddStop(0.4f, 0.f);
	//opacityTF.AddStop(0.4f, 1.f);
	//opacityTF.AddStop(.41f, 1.f);
	opacityTF.AddStop(1.f, 1.f);*/

	glActiveTexture(GL_TEXTURE3);
	opacityTF.EvaluateTexture(100);

	using ColorPLF = PLF<float, glm::vec4>;
	std::unique_ptr<HSVTransferFunction> hsvTF;
	std::unique_ptr<ColorPLF> rgbTF;
	std::string colorScheme = transferFunction["color scheme"].as<std::string>();
	if (colorScheme == std::string("HSV"))
	{
		std::array<float, 3> contrastVals = transferFunction["contrast"].as<std::array<float, 3>>();
		hsvTF = std::make_unique<HSVTransferFunction>(contrastVals);

		glActiveTexture(GL_TEXTURE2);
		hsvTF->EvaluateTexture(100);
	}
	else if (colorScheme == std::string("RGB-gradient"))
	{
		std::vector<float> stopPositions = transferFunction["contrast"].as<std::vector<float>>();
		std::vector<glm::vec3> stopColors = transferFunction["gradient"].as<std::vector<glm::vec3>>();

		if (stopPositions.front() != 0.f)
		{
			stopPositions.insert(std::begin(stopPositions), 0.f);
			stopColors.insert(std::begin(stopColors), stopColors.front());
		}

		if (stopPositions.back() != 1.f)
		{
			stopPositions.push_back(1.f);
			stopColors.push_back(stopColors.back());
		}

		rgbTF = std::make_unique<ColorPLF>();
		for (size_t i = 0; i < stopPositions.size(); i++)
		{
			rgbTF->AddStop(stopPositions[i], glm::vec4(stopColors[i], 1.f));
		}

		glActiveTexture(GL_TEXTURE2);
		rgbTF->EvaluateTexture(100);
	}

	// hard-set clearcoat PLF
	using ClearcoatPF = PLF<float, float>;
	ClearcoatPF clearcoatPF;
	clearcoatPF.AddStop(0.0f, 0.0f);
	clearcoatPF.AddStop(.65f, 0.0f);
	clearcoatPF.AddStop(0.7f, 0.7f);
	clearcoatPF.AddStop(1.0f, 0.7f);
	glActiveTexture(GL_TEXTURE7);
	clearcoatPF.EvaluateTexture(100);

	// Get cubemap file locations
	std::string cubemapFolder = config["cubemap"].as<std::string>();
	static constexpr std::array<const char*, 6> cubemapFilenames = { "posx.hdr", "negx.hdr", "posy.hdr", "negy.hdr", "posz.hdr", "negz.hdr" };
	//static constexpr std::array<const char*, 6> cubemapFilenames = { "negx.bmp", "posx.bmp", "posy.bmp", "negy.bmp", "posz.bmp", "negz.bmp" };
	std::vector<std::string> cubemapFiles;
	for (const std::string& filename : cubemapFilenames)
	{
		cubemapFiles.push_back("cubemaps/" + cubemapFolder + "/" + filename);
	}

	const GLubyte* vendor = glGetString(GL_VENDOR);
	const GLubyte* renderer = glGetString(GL_RENDERER);
	std::cout << renderer << "\n";

	glActiveTexture(GL_TEXTURE1);
	std::shared_ptr<Dicom> dicom = std::make_shared<Dicom>(scanFolder);

	const uint32_t numSamples = 8;
	RaytracePass raytracePass(size, numSamples, dicom);
	raytracePass.SetPhysicalSize(volumeScale);

	glActiveTexture(GL_TEXTURE4);
	Cubemap cubemap(cubemapFiles);

	DrawQuad drawQuad = DrawQuad(size, numSamples);

	ImageWriter imageWriter = ImageWriter(scanFolder);
	bool imageWritten = false;
	int requiredItrs = config["itrs"].as<int>();
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	while (!win->ShouldClose())
	{
		if (viewController->GetIsViewDirtied())
		{
			raytracePass.SetItrs(1);
		}

		if (requiredItrs != 0 && raytracePass.GetItrs() == requiredItrs && !imageWritten)
		{
			std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
			std::cout << "Time difference = " << std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count() << "[us]" << std::endl;
			imageWriter.WriteImage(win.get());
			imageWritten = true;
		}

		const glm::mat4 view = viewController->GetView();
		raytracePass.SetView(view);
		raytracePass.Execute();

		drawQuad.Execute(raytracePass.GetColorTexture());
		glfwPollEvents();
		win->SwapBuffers();
	}
}