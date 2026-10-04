#pragma once

#include <cstdint>

struct Entity
{
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;    // 销毁 Entity 后 ++generation，防止复用槽位、混淆当前占用该槽位的内容

    bool IsValid() const { return index != kInvalidIndex; }

    friend bool operator==(const Entity& lhs, const Entity& rhs)
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend bool operator!=(const Entity& lhs, const Entity& rhs) { return !(lhs == rhs); }
};
