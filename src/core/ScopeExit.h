#pragma once

#include <utility>

// 用法：取得临时资源后立即 ScopeExit guard([&] { 释放资源; });。
// 正常返回和异常展开都会执行；不分配内存，不可复制。回调必须不抛异常。
template<class F> class ScopeExit
{
public:
    explicit ScopeExit(F action) : m_action(std::move(action)) {}
    ~ScopeExit() noexcept { m_action(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
private:
    F m_action;
};
