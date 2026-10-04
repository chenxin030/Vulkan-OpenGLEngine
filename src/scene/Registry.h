#pragma once

#include "scene/Entity.h"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 存储 Component：一种组件类型对应一个数组。

// --------
// 按组件类型：有一个密集的 components 数组，一个平行对应的 owners 数组，以及一个从 Entity Index 到数组位置的 slot map
// 移除操作是交换并弹出，因此是 O(1)，数组不会产生空洞，即迭代是连续的
// 代价是移除会重新排序数组，所以不能依赖迭代顺序
// ---------------------------------------------------------------------------
class Registry
{
public:
    Entity Create();

    // 销毁实体并分离它拥有的每个组件
    void Destroy(Entity entity);

    bool IsAlive(Entity entity) const;

    template <typename T, typename... Args>
    T& Add(Entity entity, Args&&... args);

    template <typename T>
    bool Has(Entity entity) const;

    template <typename T>
    T* TryGet(Entity entity);

    template <typename T>
    const T* TryGet(Entity entity) const;

    template <typename T>
    void Remove(Entity entity);

    // 访问同时拥有 TFirst 和 TRest 所有组件的 Entity。
    // 可以优化成选择最小的数组，更快
    // fn 不允许创建或销毁实体。在遍历数组的同时添加或移除组件可能会导致数组在循环中被重新分配。
    template <typename TFirst, typename... TRest, typename Fn>
    void ForEach(Fn&& fn);

    std::size_t EntityCount() const { return m_liveCount; }

private:
    struct IStorage
    {
        virtual ~IStorage() = default;
        virtual void Detach(Entity entity) = 0;
    };

    template <typename T>
    struct Storage final : IStorage
    {
        std::vector<Entity> owners;                        // parallel to components
        std::vector<T> components;
        std::unordered_map<uint32_t, std::size_t> slotOf;  // entity index -> slot

        void Detach(Entity entity) override
        {
            const auto it = slotOf.find(entity.index);

            // 这次 generation 检查与 Destroy() 自己的存活性测试不是一回事：
            // Detach 也会被**陈旧句柄直接**调用（Remove<T>），那时 Destroy 根本没跑过。
            // 没有这道检查，一个槽位已被回收的句柄会分离掉当前占用者的组件。
            if (it == slotOf.end() || owners[it->second].generation != entity.generation) {
                return;
            }

            const std::size_t slot = it->second;
            const std::size_t last = components.size() - 1;

            owners[slot] = owners[last];
            components[slot] = std::move(components[last]);
            slotOf[owners[slot].index] = slot;

            owners.pop_back();
            components.pop_back();
            slotOf.erase(it);
        }

        T* Find(Entity entity)
        {
            const auto it = slotOf.find(entity.index);
            if (it == slotOf.end() || owners[it->second].generation != entity.generation) {
                return nullptr;
            }
            return &components[it->second];
        }

        const T* Find(Entity entity) const
        {
            const auto it = slotOf.find(entity.index);
            if (it == slotOf.end() || owners[it->second].generation != entity.generation) {
                return nullptr;
            }
            return &components[it->second];
        }
    };

    template <typename T>
    Storage<T>& StorageFor();

    template <typename T>
    const Storage<T>* FindStorage() const;

    std::vector<uint32_t> m_generations;
    std::vector<bool> m_alive;
    std::vector<uint32_t> m_freeIndices;
    std::size_t m_liveCount = 0;

    // One storage per component type, keyed by std::type_index. Type-erased behind
    // IStorage so a single map can hold them all; the downcast in StorageFor() is
    // checked by construction, because the key IS typeid(T).
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_storages;
};

// ---------------------------------------------------------------------------
// Template definitions
// ---------------------------------------------------------------------------

template <typename T>
Registry::Storage<T>& Registry::StorageFor()
{
    const std::type_index key(typeid(T));

    const auto it = m_storages.find(key);
    if (it != m_storages.end()) {
        return static_cast<Storage<T>&>(*it->second);
    }

    // 第一次使用某种组件时创建，一个组件类型在实际被使用之前是不会花费任何资源的。
    auto storage = std::make_unique<Storage<T>>();
    Storage<T>& reference = *storage;
    m_storages.emplace(key, std::move(storage));
    return reference;
}

template <typename T>
const Registry::Storage<T>* Registry::FindStorage() const
{
    const auto it = m_storages.find(std::type_index(typeid(T)));
    if (it == m_storages.end()) {
        return nullptr;
    }
    return static_cast<const Storage<T>*>(it->second.get());
}

template <typename T, typename... Args>
T& Registry::Add(Entity entity, Args&&... args)
{
    if (!IsAlive(entity)) {
        throw std::runtime_error("Registry::Add called with a dead or invalid entity");
    }

    Storage<T>& storage = StorageFor<T>();

    if (storage.Find(entity) != nullptr) {
        throw std::runtime_error("Registry::Add called for a component the entity already has");
    }

    storage.owners.push_back(entity);
    storage.components.emplace_back(std::forward<Args>(args)...);
    storage.slotOf[entity.index] = storage.components.size() - 1;

    return storage.components.back();
}

template <typename T>
bool Registry::Has(Entity entity) const
{
    const Storage<T>* storage = FindStorage<T>();
    return storage != nullptr && storage->Find(entity) != nullptr;
}

template <typename T>
T* Registry::TryGet(Entity entity)
{
    return StorageFor<T>().Find(entity);
}

template <typename T>
const T* Registry::TryGet(Entity entity) const
{
    const Storage<T>* storage = FindStorage<T>();
    return storage != nullptr ? storage->Find(entity) : nullptr;
}

template <typename T>
void Registry::Remove(Entity entity)
{
    StorageFor<T>().Detach(entity);
}

template <typename TFirst, typename... TRest, typename Fn>
void Registry::ForEach(Fn&& fn)
{
    Storage<TFirst>& primary = StorageFor<TFirst>();

    for (std::size_t i = 0; i < primary.owners.size(); ++i) {
        const Entity entity = primary.owners[i];

        // 用空的 TRest 会返回是 true，单组件情况不需要特殊处理。
        if (!(Has<TRest>(entity) && ...)) {
            continue;
        }

        // 用一个空的 TRest 包，会扩展为 fn(entity, component)，会如预期进行。
        // 没指定参数求值顺序，每次查找都是纯读，无法观察到顺序。
        fn(entity, primary.components[i], *TryGet<TRest>(entity)...);
    }
}
