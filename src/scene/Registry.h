#pragma once
#include "scene/Entity.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <tuple>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

// 阶段 15：同组件组合属于同一 Archetype，每个 Chunk 固定 128 行。
// 原 Storage 的密集数组 / swap-and-pop 思路保留，但交换的是整行。
// 旧实现及学习注释完整保存在 phase15-original 归档。单线程，遍历顺序不保证。
class Registry {
public:
    static constexpr std::size_t ChunkCapacity = 128;
    Registry() = default;
    Registry(const Registry& other);
    Registry& operator=(const Registry& other);
    Registry(Registry&& other) noexcept;
    Registry& operator=(Registry&& other) noexcept;
    void Swap(Registry& other) noexcept;
    Entity Create();
    void Destroy(Entity entity);
    bool IsAlive(Entity entity) const;
    std::size_t EntityCount() const { return m_liveCount; }
    template <typename Fn> void ForEachEntity(Fn&& fn) const;
    template <typename T, typename... Args> T& Add(Entity entity, Args&&... args);
    template <typename T> bool Has(Entity entity) const;
    template <typename T> T* TryGet(Entity entity);
    template <typename T> const T* TryGet(Entity entity) const;
    template <typename T> void Remove(Entity entity);
    template <typename TFirst, typename... TRest, typename Fn> void ForEach(Fn&& fn);
    template <typename TFirst, typename... TRest, typename Fn> void ForEach(Fn&& fn) const;
    // 结构变更后重新 TryGet：迁移行和补洞行的所有组件地址都可能改变。
    // 回调可改字段/嵌套查询，禁止结构变更。Swap/move/析构不能操作正在遍历的对象。
private:
    struct TypeInfo {
        std::type_index key;
        std::size_t size, alignment;
        void (*copy)(void*, const void*);
        void (*move)(void*, void*) noexcept;
        void (*destroy)(void*) noexcept;
    };
    template <typename T> static const TypeInfo& Type() {
        static_assert(std::is_copy_constructible_v<T>, "Scene copies require copyable components");
        static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>,
            "Chunk relocation requires noexcept move construction and destruction");
        static const TypeInfo info{typeid(T), sizeof(T), alignof(T),
            [](void* to, const void* from) { std::construct_at(static_cast<T*>(to), *static_cast<const T*>(from)); },
            [](void* to, void* from) noexcept { std::construct_at(static_cast<T*>(to), std::move(*static_cast<T*>(from))); },
            [](void* p) noexcept { std::destroy_at(static_cast<T*>(p)); }};
        return info;
    }
    struct Archetype;
    struct Chunk {
        explicit Chunk(const Archetype& owner);
        ~Chunk();
        Chunk(const Chunk&) = delete;
        Chunk& operator=(const Chunk&) = delete;
        void* At(std::size_t column, std::size_t row);
        const void* At(std::size_t column, std::size_t row) const;
        const Archetype& archetype;
        std::byte* data = nullptr;
        std::array<Entity, ChunkCapacity> entities{};
        std::size_t count = 0;
    };
    struct Archetype {
        explicit Archetype(std::vector<const TypeInfo*> signature);
        std::size_t Column(std::type_index key) const;
        std::vector<const TypeInfo*> types; // type_index 排序，注册顺序无关
        std::vector<std::size_t> offsets;
        std::size_t bytes = 0, alignment = alignof(std::max_align_t);
        std::vector<std::unique_ptr<Chunk>> chunks;
        std::size_t firstAvailable = 0;
    };
    struct Location { Archetype* archetype = nullptr; std::size_t chunk = 0, row = 0; };
    struct Slot { uint32_t generation = 0; bool alive = false; Location location; };
    struct Iteration {
        explicit Iteration(const Registry& r) : registry(r) { ++registry.m_iterations; }
        ~Iteration() { --registry.m_iterations; }
        const Registry& registry;
    };
    void CheckStructuralChange() const;
    Archetype& FindOrCreate(std::vector<const TypeInfo*> types);
    Location ReserveRow(Archetype& archetype);
    void EraseRow(Location location) noexcept;
    void Migrate(Entity entity, Archetype& target, const TypeInfo* added, void* value);
    // 每个 Archetype 只查一次列号，每行直接访问列；暂不保留跨调用查询缓存。
    template <typename... Ts, typename Self, typename Fn> static void Visit(Self& self, Fn&& fn) {
        Iteration iteration(self);
        for (const auto& archetype : self.m_archetypes) {
            const std::array columns{archetype->Column(typeid(Ts))...};
            if (std::find(columns.begin(), columns.end(), archetype->types.size()) != columns.end()) continue;
            for (const auto& chunk : archetype->chunks) {
                auto bases = [&]<std::size_t... I>(std::index_sequence<I...>) {
                    return std::tuple{static_cast<std::conditional_t<std::is_const_v<Self>, const Ts*, Ts*>>(
                        chunk->At(columns[I], 0))...};
                }(std::index_sequence_for<Ts...>{});
                for (std::size_t row = 0; row < chunk->count; ++row)
                    std::apply([&](auto*... base) { fn(std::as_const(chunk->entities[row]), base[row]...); }, bases);
            }
        }
    }
    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_freeIndices;
    std::size_t m_liveCount = 0;
    std::vector<std::unique_ptr<Archetype>> m_archetypes;
    mutable std::size_t m_iterations = 0;
};

template <typename Fn> void Registry::ForEachEntity(Fn&& fn) const {
    Iteration iteration(*this);
    for (std::size_t index = 0; index < m_slots.size(); ++index)
        if (m_slots[index].alive) fn(Entity{static_cast<uint32_t>(index), m_slots[index].generation});
}
template <typename T, typename... Args> T& Registry::Add(Entity entity, Args&&... args) {
    CheckStructuralChange();
    if (!IsAlive(entity)) throw std::runtime_error("Registry::Add called with a dead or invalid entity");
    if (Has<T>(entity)) throw std::runtime_error("Registry::Add called for a component the entity already has");
    // 先构造候选，允许参数引用当前组件；构造失败时原行完全不变。
    T value(std::forward<Args>(args)...);
    auto types = m_slots[entity.index].location.archetype->types;
    types.push_back(&Type<T>());
    auto& target = FindOrCreate(std::move(types));
    Migrate(entity, target, &Type<T>(), &value);
    return *TryGet<T>(entity);
}
template <typename T> bool Registry::Has(Entity entity) const { return TryGet<T>(entity) != nullptr; }
template <typename T> T* Registry::TryGet(Entity entity) {
    return const_cast<T*>(std::as_const(*this).TryGet<T>(entity));
}
template <typename T> const T* Registry::TryGet(Entity entity) const {
    if (!IsAlive(entity)) return nullptr;
    const auto& location = m_slots[entity.index].location;
    const auto column = location.archetype->Column(typeid(T));
    if (column == location.archetype->types.size()) return nullptr;
    return static_cast<const T*>(location.archetype->chunks[location.chunk]->At(column, location.row));
}
template <typename T> void Registry::Remove(Entity entity) {
    CheckStructuralChange();
    if (!Has<T>(entity)) return; // generation 检查防止陈旧句柄伤到新占用者。
    auto types = m_slots[entity.index].location.archetype->types;
    std::erase_if(types, [](const TypeInfo* type) { return type->key == typeid(T); });
    auto& target = FindOrCreate(std::move(types));
    Migrate(entity, target, nullptr, nullptr);
}
template <typename TFirst, typename... TRest, typename Fn>
void Registry::ForEach(Fn&& fn) { Visit<TFirst, TRest...>(*this, std::forward<Fn>(fn)); }
template <typename TFirst, typename... TRest, typename Fn>
void Registry::ForEach(Fn&& fn) const { Visit<TFirst, TRest...>(*this, std::forward<Fn>(fn)); }
