#include "scene/SceneSystem.h"
#include <stdexcept>

bool RuntimeEntities::Contains(Entity entity) const
{
    return entity.index < m_positions.size() && m_positions[entity.index] != Missing &&
        m_entities[m_positions[entity.index]] == entity;
}

void RuntimeEntities::Show(Entity entity)
{
    if (!entity.IsValid()) throw std::invalid_argument("Cannot show an invalid entity");
    if (Contains(entity)) return;
    if (entity.index >= m_positions.size()) m_positions.resize(std::size_t(entity.index) + 1, Missing);
    const auto previous = m_positions[entity.index];
    if (previous != Missing) Hide(m_entities[previous]); // 同一槽位的新 generation。
    m_entities.push_back(entity);
    m_positions[entity.index] = m_entities.size() - 1;
}

void RuntimeEntities::Hide(Entity entity)
{
    if (!Contains(entity)) return;
    const auto position = m_positions[entity.index];
    m_entities[position] = m_entities.back();
    m_positions[m_entities[position].index] = position;
    m_entities.pop_back();
    m_positions[entity.index] = Missing;
}
