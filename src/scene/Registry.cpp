#include "scene/Registry.h"
#include <limits>

Registry::Archetype::Archetype(std::vector<const TypeInfo*> signature) : types(std::move(signature)) {
    for (const auto* type : types) {
        alignment = std::max(alignment, type->alignment);
        bytes = (bytes + type->alignment - 1) / type->alignment * type->alignment;
        offsets.push_back(bytes);
        bytes += type->size * ChunkCapacity;
    }
}
std::size_t Registry::Archetype::Column(std::type_index key) const {
    const auto found = std::lower_bound(types.begin(), types.end(), key,
        [](const TypeInfo* type, std::type_index key) { return type->key < key; });
    return found != types.end() && (*found)->key == key ? std::size_t(found - types.begin()) : types.size();
}
Registry::Chunk::Chunk(const Archetype& owner) : archetype(owner) {
    if (owner.bytes) data = static_cast<std::byte*>(::operator new(owner.bytes, std::align_val_t(owner.alignment)));
}
Registry::Chunk::~Chunk() {
    for (std::size_t column = 0; column < archetype.types.size(); ++column)
        for (std::size_t row = 0; row < count; ++row) archetype.types[column]->destroy(At(column, row));
    if (data) ::operator delete(data, std::align_val_t(archetype.alignment));
}
void* Registry::Chunk::At(std::size_t column, std::size_t row) {
    return data + archetype.offsets[column] + archetype.types[column]->size * row;
}
const void* Registry::Chunk::At(std::size_t column, std::size_t row) const {
    return data + archetype.offsets[column] + archetype.types[column]->size * row;
}
void Registry::CheckStructuralChange() const {
    if (m_iterations) throw std::logic_error("Registry structural changes must be deferred until traversal ends");
}
Registry::Archetype& Registry::FindOrCreate(std::vector<const TypeInfo*> types) {
    std::sort(types.begin(), types.end(), [](const TypeInfo* a, const TypeInfo* b) { return a->key < b->key; });
    for (const auto& archetype : m_archetypes) if (archetype->types == types) return *archetype;
    auto candidate = std::make_unique<Archetype>(std::move(types));
    m_archetypes.push_back(std::move(candidate));
    return *m_archetypes.back();
}
Registry::Location Registry::ReserveRow(Archetype& archetype) {
    while (archetype.firstAvailable < archetype.chunks.size() &&
        archetype.chunks[archetype.firstAvailable]->count == ChunkCapacity) ++archetype.firstAvailable;
    if (archetype.firstAvailable == archetype.chunks.size())
        archetype.chunks.push_back(std::make_unique<Chunk>(archetype));
    return {&archetype, archetype.firstAvailable, archetype.chunks[archetype.firstAvailable]->count};
}
// 原存储的“交换并弹出，O(1)，不能依赖顺序”现在作用于 Chunk 整行。
// 补洞行 Entity 不变，但 location 必须立即回写，否则 TryGet 会读到旧行。
void Registry::EraseRow(Location location) noexcept {
    auto& archetype = *location.archetype;
    auto& chunk = *archetype.chunks[location.chunk];
    const auto last = chunk.count - 1;
    for (std::size_t column = 0; column < archetype.types.size(); ++column) {
        const auto& type = *archetype.types[column];
        type.destroy(chunk.At(column, location.row));
        if (location.row != last) {
            type.move(chunk.At(column, location.row), chunk.At(column, last));
            type.destroy(chunk.At(column, last));
        }
    }
    if (location.row != last) {
        chunk.entities[location.row] = chunk.entities[last];
        m_slots[chunk.entities[location.row].index].location = location;
    }
    --chunk.count;
    archetype.firstAvailable = std::min(archetype.firstAvailable, location.chunk);
    // 空 Chunk 保留内存以复用分配；不是货物对象池，组件生命周期真正结束。
}
void Registry::Migrate(Entity entity, Archetype& target, const TypeInfo* added, void* value) {
    const auto source = m_slots[entity.index].location;
    const auto destination = ReserveRow(target); // 分配失败不改原行；以下均 noexcept。
    auto& from = *source.archetype->chunks[source.chunk];
    auto& to = *target.chunks[destination.chunk];
    for (std::size_t column = 0; column < target.types.size(); ++column) {
        const auto* type = target.types[column];
        void* input = type == added ? value : from.At(source.archetype->Column(type->key), source.row);
        type->move(to.At(column, destination.row), input);
    }
    to.entities[destination.row] = entity;
    ++to.count;
    EraseRow(source);
    m_slots[entity.index].location = destination;
}
Entity Registry::Create() {
    CheckStructuralChange();
    const bool reuse = !m_freeIndices.empty();
    if (!reuse && m_slots.size() >= Entity::kInvalidIndex) throw std::length_error("Registry entity indices exhausted");
    auto& empty = FindOrCreate({});
    const auto location = ReserveRow(empty);
    uint32_t index;
    if (reuse) { index = m_freeIndices.back(); m_freeIndices.pop_back(); }
    else { index = static_cast<uint32_t>(m_slots.size()); m_slots.emplace_back(); }
    auto& slot = m_slots[index];
    slot.alive = true;
    slot.location = location;
    const Entity entity{index, slot.generation};
    auto& chunk = *empty.chunks[location.chunk];
    chunk.entities[location.row] = entity;
    ++chunk.count;
    ++m_liveCount;
    return entity;
}
void Registry::Destroy(Entity entity) {
    CheckStructuralChange();
    if (!IsAlive(entity)) return;
    auto& slot = m_slots[entity.index];
    // generation 耗尽时永久退休，避免绕回旧句柄。分配失败也不能半销毁。
    if (slot.generation != std::numeric_limits<uint32_t>::max()) m_freeIndices.push_back(entity.index);
    EraseRow(slot.location);
    slot.alive = false;
    slot.location = {};
    if (slot.generation != std::numeric_limits<uint32_t>::max()) ++slot.generation;
    --m_liveCount;
}
bool Registry::IsAlive(Entity entity) const {
    return entity.index < m_slots.size() && m_slots[entity.index].alive &&
        m_slots[entity.index].generation == entity.generation;
}
Registry::Registry(const Registry& other)
    : m_slots(other.m_slots), m_freeIndices(other.m_freeIndices), m_liveCount(other.m_liveCount) {
    for (const auto& source : other.m_archetypes) {
        auto& target = FindOrCreate(source->types);
        for (const auto& from : source->chunks) {
            auto candidate = std::make_unique<Chunk>(target);
            for (std::size_t row = 0; row < from->count; ++row) {
                std::size_t column = 0;
                try {
                    for (; column < target.types.size(); ++column)
                        target.types[column]->copy(candidate->At(column, row), from->At(column, row));
                } catch (...) {
                    // count 只含完整行；显式销毁失败行已构造的前缀。
                    while (column) { --column; target.types[column]->destroy(candidate->At(column, row)); }
                    throw;
                }
                candidate->entities[row] = from->entities[row];
                ++candidate->count;
            }
            target.chunks.push_back(std::move(candidate));
            for (std::size_t row = 0; row < from->count; ++row)
                m_slots[from->entities[row].index].location = {&target, target.chunks.size() - 1, row};
        }
    }
}
Registry& Registry::operator=(const Registry& other) {
    CheckStructuralChange();
    if (this != &other) { Registry copy(other); Swap(copy); }
    return *this;
}
Registry::Registry(Registry&& other) noexcept { Swap(other); }
Registry& Registry::operator=(Registry&& other) noexcept {
    if (this != &other) { Registry moved(std::move(other)); Swap(moved); }
    return *this;
}
void Registry::Swap(Registry& other) noexcept {
    if (m_iterations || other.m_iterations) std::terminate();
    using std::swap;
    swap(m_slots, other.m_slots);
    swap(m_freeIndices, other.m_freeIndices);
    swap(m_liveCount, other.m_liveCount);
    swap(m_archetypes, other.m_archetypes);
}
