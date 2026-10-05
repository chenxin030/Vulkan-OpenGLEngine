#pragma once

class Registry;

// 虚函数一般无法内联，如果做成类的一个虚函数，还要再产生一次虚表间接调用的开销
// 虚拟 system 接口可以实现运行时注册系统和动态排序，目前还不需要。

void SpinSystem(Registry& registry, float deltaSeconds);

void TransformSystem(Registry& registry);

// 在 TransformSystem **之后**跑：它依赖 TransformSystem 的 pass 1 已经重置过 world。
// 取绝对时间而不是 dt：浮动的相位不该被帧率影响。
void BobSystem(Registry& registry, float elapsedSeconds);
