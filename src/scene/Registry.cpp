#include "scene/Registry.h"

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
