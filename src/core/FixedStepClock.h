#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

// 固定 60 Hz 模拟；长帧分批追赶，保留未执行的积压时间。
// 渲染跑到 120 FPS 时，大致每两帧执行一次模拟；
// 渲染只有 30 FPS 时，大致每帧执行两次模拟，让模拟速度不随渲染帧率变化。
class FixedStepClock
{
public:
    static constexpr double Step = 1.0 / 60.0;
    // 把这一帧经过的时间，换算成本帧该执行多少次固定模拟更新。
    // 返回更新的次数
    uint32_t Advance(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::invalid_argument("Invalid frame delta");
        m_accumulator += seconds;
        uint32_t steps = 0;
        while (m_accumulator + 1e-12 >= Step && steps < 8) {
            m_accumulator -= Step;
            ++steps;
        }
        m_accumulator = std::max(0.0, m_accumulator);
        return steps;
    }
    double BacklogSeconds() const { return m_accumulator; }
    void Reset() { m_accumulator = 0; }
private:
    double m_accumulator = 0;
};
