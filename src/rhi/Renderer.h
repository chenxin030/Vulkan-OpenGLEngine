#pragma once

struct GLFWwindow;

class Renderer
{
public:
	Renderer() = default;
	virtual ~Renderer() = default;

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	virtual void ConfigureWindowHints() const = 0;
	virtual void Init(GLFWwindow* window) = 0;
	virtual void DrawFrame() = 0;
	virtual void WaitIdle() = 0;
	virtual void Cleanup() = 0;
};
