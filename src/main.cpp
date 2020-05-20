#include <string>
#include <iostream>
#include <memory>
#include <optional>
#include <algorithm>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>

#include "Dicom.h"
#include "Window.h"
#include "RaytracePass.h"
#include "DrawQuad.h"
#include "PiecewiseFunction.h"
#include "GLObjects.h"

class ViewController : public MouseListener
{
public:
	ViewController(const glm::mat4& initial = glm::mat4(1.f))
		: mLastCachedView(initial)
		, mDragStartPos()
		, mCurrDelta()
		, mViewDirtied(false)
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
		const glm::mat4 azRot = glm::rotate(rotDelta.x, glm::vec3(0.f, 1.f, 0.f));
		const glm::mat4 altRot = glm::rotate(rotDelta.y, glm::vec3(1.f, 0.f, 0.f));
		return azRot * altRot;
	}

	std::optional<glm::vec2> mDragStartPos;
	std::optional<glm::mat4> mCurrDelta;
	glm::mat4 mLastCachedView;
	bool mViewDirtied;
};

int main()
{
	glm::ivec2 size = glm::ivec2(1920, 1080);
	std::shared_ptr<Window> win = std::make_shared<Window>(size, "Cinematic Renderer");

	const glm::mat4 initialView = glm::translate(glm::vec3(0.f, 0.f, -3.f));
	std::shared_ptr<ViewController> viewController = std::make_shared<ViewController>(initialView);
	win->AddMouseListener(viewController);

	const GLubyte* vendor = glGetString(GL_VENDOR);
	const GLubyte* renderer = glGetString(GL_RENDERER);
	std::cout << renderer << "\n";

	std::string folder = "scans/Larry_2017/";
	//std::string folder = "scans/Larry_Smarr_2016/";
	//std::string folder = "scans/HNSCC/HNSCC-01-0617/12-16-2012-017-27939/2.000000-ORALNASOPHARYNX-41991/";
	//std::string folder = "scans/HNSCC/HNSCC-01-0620/12-16-2012-002-29827/2.000000-ORALNASOPHARYNX-84704/";
	//std::string folder = "scans/HNSCC/HNSCC-01-0618/12-16-2012-002-07039/2.000000-ORALNASOPHARYNX-35559/";
	glActiveTexture(GL_TEXTURE1);
	std::shared_ptr<Dicom> dicom = std::make_shared<Dicom>(folder);

	using ColorPF = PLF<float, glm::vec4>;
	ColorPF colorPF;
	colorPF.AddStop(0.f, glm::vec4(1.f, 0.f, 0.f, 1.f));
	colorPF.AddStop(.6f, glm::vec4(1.f, 0.f, 0.f, 1.f));
	colorPF.AddStop(.8f, glm::vec4(1.f, .3f, 0.f, 1.f));
	colorPF.AddStop(1.f, glm::vec4(1.f, .3f, 0.f, 1.f));

	// warren's transfer func
	//colorPF.AddStop(0.f, glm::vec4(.62f, .62f, .64f, 1.f));
	//colorPF.AddStop(0.2361297f, glm::vec4(.17f, 0.f, 0.f, 1.f));
	//colorPF.AddStop(0.288528f, glm::vec4(.17f, 0.02f, 0.02f, 1.f));
	//colorPF.AddStop(0.288558f, glm::vec4(0.26f, 0.f, 0.f, 1.f));
	//colorPF.AddStop(0.354003f, glm::vec4(0.337f, 0.282f, 0.16f, 1.f));
	//colorPF.AddStop(1.f, glm::vec4(0.62745f, 0.62745f, 0.64313f, 1.f));

	glActiveTexture(GL_TEXTURE2);
	colorPF.EvaluateTexture(100);

	using OpacityPF = PLF<float, float>;
	OpacityPF opacityPF;
	opacityPF.AddStop(0.f, 0.f);
	//opacityPF.AddStop(.5f, 0.f);
	opacityPF.AddStop(.3f, 0.f);
	opacityPF.AddStop(1.f, .5f);

	glActiveTexture(GL_TEXTURE3);
	opacityPF.EvaluateTexture(100);

	using ClearcoatPF = PLF<float, float>;
	ClearcoatPF clearcoatPF;
	clearcoatPF.AddStop(0.f, 0.f);
	clearcoatPF.AddStop(.7f, 0.f);
	clearcoatPF.AddStop(.8f, .05f);
	clearcoatPF.AddStop(1.f, .05f);

	glActiveTexture(GL_TEXTURE8);
	clearcoatPF.EvaluateTexture(100);

	std::string irrCubemapFolder = "cubemaps/studio1/";
	std::vector<std::string> irrCubemapFiles = {
		irrCubemapFolder + "posx.hdr", irrCubemapFolder + "negx.hdr",
		irrCubemapFolder + "posy.hdr", irrCubemapFolder + "negy.hdr",
		irrCubemapFolder + "posz.hdr", irrCubemapFolder + "negz.hdr"
	};
	glActiveTexture(GL_TEXTURE4);
	Cubemap irrCubemap(irrCubemapFiles);

	std::string cubemapFolder = "cubemaps/indoors/";
	std::vector<std::string> cubemapFiles = {
		cubemapFolder + "posx.bmp", cubemapFolder + "negx.bmp",
		cubemapFolder + "posy.bmp", cubemapFolder + "negy.bmp",
		cubemapFolder + "posz.bmp", cubemapFolder + "negz.bmp"
	};
	glActiveTexture(GL_TEXTURE7);
	Cubemap cubemap(cubemapFiles);

	const uint32_t numSamples = 1;
	RaytracePass raytracePass(size, numSamples, dicom);
	DrawQuad drawQuad = DrawQuad(size, numSamples);

	while (!win->ShouldClose())
	{
		if (viewController->GetIsViewDirtied())
		{
			raytracePass.SetItrs(1);
		}

		const glm::mat4 view = viewController->GetView();
		raytracePass.SetView(view);
		raytracePass.Execute();

		// make sure writing to image has finished before read
		//glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//glClear(GL_COLOR_BUFFER_BIT);
		drawQuad.Execute(raytracePass.GetColorTexture());
		glfwPollEvents();
		win->SwapBuffers();
	}
}