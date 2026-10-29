#pragma once

#include "platform/InputState.h"
#include "rhi/Viewport.h"
#include "scene/RenderSnapshot.h"
#include <optional>
#include <stdexcept>
#include <utility>

// 每次 RunFrame 的局部对象。Layer 可以提交一份快照；不能跨帧保存本对象或其中的引用。用这个跨帧传递信息
struct FrameContext
{
    float deltaSeconds = 0;                 // FrameContext 是一帧内共享的数据包：Application 填入时间和输入，Layer 填入渲染请求和快照，渲染器再提供图像信息供 UI 使用。
    InputState input;                       
    glm::vec2 windowFramebufferExtent{0};   // 整个窗口的实际像素尺寸
    ViewportRequest renderViewportRequest;  // 场景渲染区域的请求尺寸
    ViewportImage uiViewportImage;          // UI 回调前由 Renderer 提供；不跨帧缓存句柄。
    Camera renderCamera;                    // 与当前快照一致，拾取/操纵轴不读取稍后 UI 编辑的相机。

    std::optional<RenderSnapshot> m_snapshot;

    void SubmitRenderSnapShot(RenderSnapshot snapshot)
    {
        if (m_snapshot) 
            throw std::logic_error("Only one scene snapshot may be submitted per frame");
        m_snapshot.emplace(std::move(snapshot));
    }

};
