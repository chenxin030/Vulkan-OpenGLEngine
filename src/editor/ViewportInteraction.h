#pragma once

#include "rhi/Viewport.h"
#include "platform/InputState.h"
#include <algorithm>
#include <cmath>

// 编辑器的逻辑矩形/输入计算；不放在 RHI 中，Renderer 不依赖相机交互规则。
struct ViewportRect
{
    glm::vec2 min{0}, size{0};
    bool Contains(glm::vec2 point) const
    {
        return size.x > 0 && size.y > 0 && point.x >= min.x && point.y >= min.y &&
            point.x < min.x + size.x && point.y < min.y + size.y;
    }
};

// 从逻辑单位转换到 framebuffer 像素；非法/零尺寸返回空，设备上限在后端再次裁剪。
inline ViewportExtent ViewportPixels(glm::vec2 logical, glm::vec2 scale, uint32_t limit = 16384)
{
    if (!limit || !std::isfinite(logical.x) || !std::isfinite(logical.y) ||
        !std::isfinite(scale.x) || !std::isfinite(scale.y) || logical.x <= 0 ||
        logical.y <= 0 || scale.x <= 0 || scale.y <= 0) return {};
    const auto pixel = [limit](float value, float factor) {
        return uint32_t(std::clamp(std::round(double(value) * factor), 1.0, double(limit)));
    };
    return {pixel(logical.x, scale.x), pixel(logical.y, scale.y)};
}

// 请求尚未生效时仍等比例显示当前目标。逻辑宽高比补偿非均匀 framebuffer scale。
inline ViewportRect FitViewport(glm::vec2 min, glm::vec2 available, ViewportExtent extent,
    glm::vec2 scale)
{
    if (!extent.Valid() || !ViewportPixels(available, scale).Valid()) return {min, {0, 0}};
    const float aspect = extent.Aspect() * scale.y / scale.x;
    glm::vec2 size{available.x, available.x / aspect};
    if (size.y > available.y) size = {available.y * aspect, available.y};
    return {min + (available - size) * 0.5f, size};
}

// 当前光标再次检查上一帧 Image 边界；相机准入不改变 Application 的 Escape 规则。
// admitted 记录上次是否准入，开始/恢复时清空 lookDelta，避免跨面板累积鼠标移动。
inline InputState FilterViewportCamera(InputState input, const ViewportRect& rect,
    bool focused, bool gesture, bool blocked, bool& admitted)
{
    const bool allow = focused && gesture && !blocked && input.lookActive && rect.Contains(input.cursorPosition);
    if (!allow) {
        input.moveAxis = glm::vec3(0); input.sprint = false;
        input.lookActive = false; input.lookDelta = glm::vec2(0);
    } else if (!admitted) input.lookDelta = glm::vec2(0);
    admitted = allow;
    return input;
}
