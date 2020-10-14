#include "Window.h"

#include <stdexcept>
#include <unordered_map>

std::unordered_map<GLFWwindow*, std::shared_ptr<Window>> gWindowMap;

Window::Window(const glm::ivec2& size, std::string title)
	: std::enable_shared_from_this<Window>()
{
	if (!glfwInit())
	{
		throw std::runtime_error("failed to init glfw");
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
	mWindow = glfwCreateWindow(size.x, size.y, title.c_str(), nullptr, nullptr);
	if (!mWindow)
	{
		// Window or context creation failed
		throw std::runtime_error("context creation failed");
	}

	glfwMakeContextCurrent(mWindow);

	if (glewInit())
	{
		throw std::runtime_error("failed to init glew");
	}

	glfwSwapInterval(1);

	// fixed viewport for now
	int width, height;
	glfwGetFramebufferSize(mWindow, &width, &height);
	glViewport(0, 0, width, height);
	mFramebufferSize = glm::ivec2(width, height);

	glfwSetCursorPosCallback(mWindow, [](GLFWwindow* window, double xpos, double ypos) {
		for (auto& listener : gWindowMap[window]->mListeners)
		{
			listener.lock()->HandleMotion(gWindowMap[window], glm::vec2(xpos, ypos));
		}
	});

	glfwSetMouseButtonCallback(mWindow, [](GLFWwindow* window, int button, int action, int mods) {
		for (auto& listener : gWindowMap[window]->mListeners)
		{
			listener.lock()->HandleButton(gWindowMap[window], button, action, mods);
		}
	});

	glfwSetScrollCallback(mWindow, [](GLFWwindow* window, double xoffset, double yoffset) {
		for (auto& listener : gWindowMap[window]->mListeners)
		{
			listener.lock()->HandleScroll(gWindowMap[window], glm::dvec2(xoffset, yoffset));
		}
	});
}

Window::~Window()
{
	glfwDestroyWindow(mWindow);
}

glm::vec2 Window::GetMousePos() const
{
	double xpos, ypos;
	glfwGetCursorPos(mWindow, &xpos, &ypos);
	return glm::vec2(xpos, ypos);
}

void Window::AddMouseListener(std::shared_ptr<MouseListener> listener) 
{ 
	if (gWindowMap.find(mWindow) == gWindowMap.end())
	{
		gWindowMap[mWindow] = shared_from_this();
	}

	mListeners.push_back(listener); 
}