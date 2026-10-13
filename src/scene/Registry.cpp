#include "scene/Registry.h"

#include <utility>

Registry::Registry(const Registry& other)
    : m_generations(other.m_generations),
      m_alive(other.m_alive),
      m_freeIndices(other.m_freeIndices),
      m_liveCount(other.m_liveCount)
{
    for (const auto& [type, storage] : other.m_storages) {
        m_storages.emplace(type, storage->Clone());
    }
}

Registry& Registry::operator=(const Registry& other)
{
    if (this != &other) {
        Registry copy(other);
        Swap(copy);
    }
    return *this;
}

void Registry::Swap(Registry& other) noexcept
{
    using std::swap;
    swap(m_generations, other.m_generations);
    swap(m_alive, other.m_alive);
    swap(m_freeIndices, other.m_freeIndices);
    swap(m_liveCount, other.m_liveCount);
    swap(m_storages, other.m_storages);
}

Entity Registry::Create()
{
    uint32_t index = 0;
    // 复用槽位
    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_alive[index] = true;
    }
    else {
        index = static_cast<uint32_t>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(true);
    }

    ++m_liveCount;

    Entity entity;
    entity.index = index;
    entity.generation = m_generations[index];
    return entity;
}

void Registry::Destroy(Entity entity)
{
    if (!IsAlive(entity)) {
        return;
    }

    for (auto& entry : m_storages) {
        entry.second->Detach(entity);
    }

    m_alive[entity.index] = false;

    ++m_generations[entity.index];

    m_freeIndices.push_back(entity.index);
    --m_liveCount;
}

bool Registry::IsAlive(Entity entity) const
{
    return entity.index < m_generations.size() &&
        m_alive[entity.index] &&
        m_generations[entity.index] == entity.generation;
}
