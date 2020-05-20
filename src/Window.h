#pragma once

#include <string>
#include <memory>
#include <vector>

#include <gl/glew.h>
#include <GLFW/glfw3.h>
#include <glm/fwd.hpp>

class Window;

class MouseListener
{
public:
	virtual void HandleMotion(std::shared_ptr<Window> window, const glm::vec2& pos) {}
	virtual void HandleButton(std::shared_ptr<Window> window, int button, int action, int mods) {}
	virtual void HandleScroll(std::shared_ptr<Window> window, const glm::dvec2& offset) {}
};

class Window : public std::enable_shared_from_this<Window>
{
public:
	Window(const glm::ivec2& size, std::string title);
	~Window();

	inline bool ShouldClose() const { return glfwWindowShouldClose(mWindow); }
	inline void SwapBuffers() { glfwSwapBuffers(mWindow); }

	glm::vec2 GetMousePos() const;
	void AddMouseListener(std::shared_ptr<MouseListener> listener);

private:
	std::vector<std::weak_ptr<MouseListener>> mListeners;
	GLFWwindow* mWindow;
};

