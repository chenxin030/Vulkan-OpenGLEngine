#pragma once

#include <functional>
#include "rhi/Viewport.h"

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
	// 设置下一次 DrawFrame 的请求；Image 仅在该帧 UI 回调期间借用。
	void SetViewportRequest(ViewportRequest request) { m_viewportRequest = request; }
	virtual ViewportImage GetViewportImage() const { return {}; }
	virtual void WaitIdle() = 0;
	// Application 在正常退出时先 WaitIdle；Cleanup 也必须支持 Init 中途失败及重复调用。
	virtual void Cleanup() = 0;
protected:
	ViewportRequest m_viewportRequest;
};
