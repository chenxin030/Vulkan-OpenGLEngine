#pragma once

#include "platform/InputState.h"
#include "scene/RenderSnapshot.h"
#include <optional>
#include <stdexcept>
#include <utility>

class LayerStack;

// 每次 RunFrame 的局部对象。Layer 可以提交一份快照；不能跨帧保存本对象或其中的引用。用这个跨帧传递信息
class FrameContext
{
public:
    float deltaSeconds = 0;
    InputState input;
    glm::vec2 framebufferSize{0};

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
