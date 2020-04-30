#include <string>
#include <iostream>
#include <memory>
#include <optional>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/gtx/transform.hpp>

#include "Dicom.h"
#include "Window.h"
#include "RaytracePass.h"
#include "DrawQuad.h"
#include "PiecewiseFunction.h"
#include "Cubemap.h"

class ViewController : public MouseListener
{
public:
	ViewController(const glm::mat4& initial = glm::mat4(1.f))
		: mLastCachedView(initial)
		, mDragStartPos()
		, mCurrDelta()
	{}

	virtual void HandleMotion(std::shared_ptr<Window> window, const glm::vec2& pos) override
	{
		if (mDragStartPos)
		{
			mCurrDelta = CalcRot(window);
		}
	}

	virtual void HandleButton(std::shared_ptr<Window> window, int button, int action, int mods) override
	{
		if (button == GLFW_MOUSE_BUTTON_LEFT)
		{
			if (action == GLFW_PRESS)
			{
				mDragStartPos = window->GetMousePos();
				mCurrDelta = glm::mat4(1.f);
			}
			else if (action == GLFW_RELEASE)
			{
				mDragStartPos.reset();
				mCurrDelta.reset();
				mLastCachedView = *mCurrDelta * mLastCachedView;
			}
		}
	}

	glm::mat4 GetView()
	{
		if (mDragStartPos)
		{
			return *mCurrDelta * mLastCachedView;
		}
		return mLastCachedView;
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
};

int main()
{
	glm::ivec2 size = glm::ivec2(1280, 720);
	std::shared_ptr<Window> win = std::make_shared<Window>(size, "Cinematic Renderer");

	const glm::mat4 initialView = glm::translate(glm::vec3(0.f, 0.f, -3.f));
	std::shared_ptr<ViewController> viewController = std::make_shared<ViewController>(initialView);
	win->AddMouseListener(viewController);

	const GLubyte* vendor = glGetString(GL_VENDOR);
	const GLubyte* renderer = glGetString(GL_RENDERER);
	std::cout << renderer << "\n";

	std::string folder = "scans/Larry_Smarr_2016/";
	glActiveTexture(GL_TEXTURE1);
	std::shared_ptr<Dicom> dicom = std::make_shared<Dicom>(folder);

	using ColorPF = PLF<float, glm::vec4>;
	ColorPF colorPF;
	colorPF.AddStop(0.f, glm::vec4(0.f, 0.f, 0.f, 1.f));
	colorPF.AddStop(1.f, glm::vec4(1.f, 1.f, 1.f, 1.f));

	//colorPF.AddStop(0.f, glm::vec4(.62f, .62f, .64f, 1.f));
	//colorPF.AddStop(0.2361297f, glm::vec4(.17f, 0.f, 0.f, 1.f));
	//colorPF.AddStop(0.288528f, glm::vec4(.17f, 0.02f, 0.02f, 1.f));
	//colorPF.AddStop(0.288558f, glm::vec4(0.26f, 0.f, 0.f, 1.f));
	//colorPF.AddStop(0.354003f, glm::vec4(0.337f, 0.282f, 0.16f, 1.f));
	//colorPF.AddStop(1.f, glm::vec4(0.62745f, 0.62745f, 0.64313f, 1.f));

	glActiveTexture(GL_TEXTURE2);
	colorPF.EvaluateTexture(100);

	using OpacityPF = PCF<float, float>;
	OpacityPF opacityPF;
	opacityPF.AddStop(0.f, 0.f);
	opacityPF.AddStop(.1f, 1.f);
	opacityPF.AddStop(1.f, 1.f);

	glActiveTexture(GL_TEXTURE3);
	opacityPF.EvaluateTexture(100);

	std::string cubemapFolder = "cubemaps/indoors_irr/";
	std::vector<std::string> cubemapFiles = {
		cubemapFolder + "negx.bmp", cubemapFolder + "posx.bmp",
		cubemapFolder + "posy.bmp", cubemapFolder + "negy.bmp",
		cubemapFolder + "posz.bmp", cubemapFolder + "negz.bmp"
	};
	glActiveTexture(GL_TEXTURE4);
	Cubemap cubemap(cubemapFiles);

	const uint32_t numSamples = 8;
	RaytracePass raytracePass(size, numSamples, dicom);
	DrawQuad drawQuad = DrawQuad(size, numSamples);

	while (!win->ShouldClose())
	{
		const glm::mat4 view = viewController->GetView();
		raytracePass.SetView(view);
		raytracePass.Execute();

		// make sure writing to image has finished before read
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		//glClear(GL_COLOR_BUFFER_BIT);
		drawQuad.Execute(raytracePass.GetColorTexture());
		glfwPollEvents();
		win->SwapBuffers();
	}
}