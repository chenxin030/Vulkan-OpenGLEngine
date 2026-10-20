#pragma once

#include "platform/InputState.h"
#include "rhi/Viewport.h"
#include "scene/RenderSnapshot.h"
#include <optional>
#include <chrono>
#include <stdexcept>
#include <utility>

class LayerStack;

// 每次 RunFrame 的局部对象。Layer 可以提交一份快照；不能跨帧保存本对象或其中的引用。用这个跨帧传递信息
class FrameContext
{
public:
    std::chrono::steady_clock::time_point frameStarted = std::chrono::steady_clock::now();
    float deltaSeconds = 0;
    InputState input;
    glm::vec2 framebufferSize{0};
    ViewportRequest viewportRequest;
    ViewportImage viewportImage; // UI 回调前由 Renderer 提供；不跨帧缓存句柄。
    Camera renderCamera; // 与当前快照一致，拾取/操纵轴不读取稍后 UI 编辑的相机。

    void Submit(RenderSnapshot snapshot)
    {
        if (m_snapshot) throw std::logic_error("Only one scene snapshot may be submitted per frame");
        m_snapshot.emplace(std::move(snapshot));
    }
    bool HasSnapshot() const { return m_snapshot.has_value(); }

private:
    friend class LayerStack;
    std::optional<RenderSnapshot> m_snapshot;
};
