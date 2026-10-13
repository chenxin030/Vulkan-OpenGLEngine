#pragma once

#include <functional>

struct GLFWwindow;
struct RenderSnapshot;

class Renderer
{
public:
	Renderer() = default;
	virtual ~Renderer() = default;

	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	virtual void ConfigureWindowHints() const = 0;
	virtual void Init(GLFWwindow* window) = 0;
	// 同步消费快照和回调，不保存其中引用。跳帧允许不调用 UI；实际 ImGui 帧只调用一次。
	virtual void DrawFrame(const RenderSnapshot& snapshot,
		const std::function<void()>& drawEditorUi) = 0;
	virtual void WaitIdle() = 0;
	// Application 在正常退出时先 WaitIdle；Cleanup 也必须支持 Init 中途失败及重复调用。
	virtual void Cleanup() = 0;
};
