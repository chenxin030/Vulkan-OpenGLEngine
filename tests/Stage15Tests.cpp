#include "scene/Registry.h"
#include "scene/Components.h"
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
#define CHECK15(...) do { ++assertions; if (!(__VA_ARGS__)) { \
    std::cerr << "Stage15 failed at " << __LINE__ << ": " << #__VA_ARGS__ << '\n'; return false; } } while (false)
struct alignas(256) Aligned { int value = 0; };
struct Tracked {
    static inline int alive = 0, copiesBeforeThrow = -1;
    std::string value;
    explicit Tracked(std::string text = {}) : value(std::move(text)) { ++alive; }
    Tracked(const Tracked& other) : value(other.value) {
        if (copiesBeforeThrow == 0) throw std::runtime_error("injected copy failure");
        if (copiesBeforeThrow > 0) --copiesBeforeThrow;
        ++alive;
    }
    Tracked(Tracked&& other) noexcept : value(std::move(other.value)) { ++alive; }
    ~Tracked() { --alive; }
};
struct Fails {
    explicit Fails(int) { throw std::runtime_error("injected constructor failure"); }
    Fails(const Fails&) = default;
    Fails(Fails&&) noexcept = default;
};

bool ChunkBoundariesAndCopy(int& assertions)
{
    Registry registry;
    std::vector<Entity> entities;
    // 跨三块边界，混合添加顺序：同一组件集合应仍在相同列布局中。
    for (int i = 0; i < 385; ++i) {
        const auto entity = registry.Create();
        entities.push_back(entity);
        if (i % 2) registry.Add<Aligned>(entity, Aligned{i});
        registry.Add<Tracked>(entity, std::string(200, char('a' + i % 26)));
        if (!(i % 2)) registry.Add<Aligned>(entity, Aligned{i});
        registry.Add<Name>(entity, Name{std::to_string(i)});
    }
    CHECK15(Tracked::alive == 385);
    bool aligned = true, values = true;
    int visited = 0;
    registry.ForEach<Aligned, Tracked, Name>([&](Entity e, Aligned& a, Tracked& t, Name& n) {
        aligned &= reinterpret_cast<std::uintptr_t>(&a) % 256 == 0;
        values &= e == entities[a.value] && n.value == std::to_string(a.value) &&
            t.value == std::string(200, char('a' + a.value % 26));
        ++visited;
    });
    CHECK15(aligned && values && visited == 385);
    for (int i : {0, 127, 128, 256, 384}) registry.Remove<Aligned>(entities[i]);
    registry.Destroy(entities[1]); // 补洞时搬移字符串；旧句柄必须无害。
    const auto replacement = registry.Create();
    CHECK15(replacement.index == entities[1].index && replacement.generation != entities[1].generation);
    registry.Add<Name>(replacement, Name{"replacement"});
    registry.Remove<Name>(entities[1]);
    CHECK15(registry.TryGet<Name>(replacement)->value == "replacement");
    Registry copy = registry;
    CHECK15(Tracked::alive == 768);
    copy.TryGet<Tracked>(entities[2])->value = "independent";
    CHECK15(registry.TryGet<Tracked>(entities[2])->value.size() == 200);
    // 深复制失败不能污染源或目标；已构造的部分行必须销毁。
    Registry target;
    const auto saved = target.Create();
    target.Add<Name>(saved, Name{"keep"});
    for (int budget : {0, 1, 127, 128, 300}) {
        Tracked::copiesBeforeThrow = budget;
        bool threw = false;
        try { target = registry; } catch (const std::runtime_error&) { threw = true; }
        Tracked::copiesBeforeThrow = -1;
        CHECK15(threw && target.EntityCount() == 1 && target.TryGet<Name>(saved)->value == "keep");
        CHECK15(Tracked::alive == 768);
    }
    bool threw = false;
    try { registry.Add<Fails>(entities[2], 1); } catch (const std::runtime_error&) { threw = true; }
    CHECK15(threw && !registry.Has<Fails>(entities[2]) && registry.TryGet<Tracked>(entities[2])->value.size() == 200);
    Registry moved(std::move(copy));
    CHECK15(copy.EntityCount() == 0 && moved.EntityCount() == 385);
    copy.Add<Name>(copy.Create(), Name{"reusable moved-from"});
    target = std::move(moved);
    CHECK15(moved.EntityCount() == 0 && target.TryGet<Tracked>(entities[2])->value == "independent");
    target.Swap(copy);
    CHECK15(target.EntityCount() == 1 && copy.EntityCount() == 385);
    copy = copy;
    copy = std::move(copy);
    CHECK15(copy.TryGet<Tracked>(entities[2])->value == "independent");
    return true;
}

bool RandomizedModel(int& assertions)
{
    Registry registry;
    struct Expected { Entity e; bool alive = true, name = false, aligned = false; };
    std::vector<Expected> model;
    std::mt19937 random(150015);
    for (int step = 0; step < 12000; ++step) {
        if (model.empty() || random() % 5 == 0) model.push_back({registry.Create()});
        auto& x = model[random() % model.size()];
        if (x.alive) {
            switch (random() % 5) {
            case 0:
                if (!x.name) registry.Add<Name>(x.e, Name{std::to_string(x.e.index)});
                x.name = true; break;
            case 1: registry.Remove<Name>(x.e); x.name = false; break;
            case 2:
                if (!x.aligned) registry.Add<Aligned>(x.e, Aligned{int(x.e.index)});
                x.aligned = true; break;
            case 3: registry.Remove<Aligned>(x.e); x.aligned = false; break;
            case 4: registry.Destroy(x.e); x.alive = false; break;
            }
        } else {
            registry.Destroy(x.e);
            registry.Remove<Name>(x.e);
            CHECK15(!registry.IsAlive(x.e) && !registry.Has<Name>(x.e));
        }
        if (step % 100 != 0) continue;
        int live = 0, matches = 0;
        for (const auto& expected : model) {
            CHECK15(registry.IsAlive(expected.e) == expected.alive);
            if (!expected.alive) continue;
            ++live;
            CHECK15(registry.Has<Name>(expected.e) == expected.name);
            CHECK15(registry.Has<Aligned>(expected.e) == expected.aligned);
            if (expected.name) CHECK15(registry.TryGet<Name>(expected.e)->value == std::to_string(expected.e.index));
            if (expected.aligned) CHECK15(registry.TryGet<Aligned>(expected.e)->value == int(expected.e.index));
            matches += expected.name && expected.aligned;
        }
        int visited = 0;
        std::as_const(registry).ForEach<Name, Aligned>([&](Entity, const Name&, const Aligned&) { ++visited; });
        CHECK15(registry.EntityCount() == live && visited == matches);
    }
    return true;
}

bool TraversalContract(int& assertions)
{
    Registry registry;
    auto entity = registry.Create();
    registry.Add<Name>(entity, Name{"original"});
    registry.ForEach<Name>([](auto& owner, Name&) {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(owner)>>,
            "Queries must not expose mutable ownership handles");
    });
    int rejected = 0;
    registry.ForEach<Name>([&](Entity e, Name& name) {
        name.value = "field update";
        try { registry.Add<Aligned>(e); } catch (const std::logic_error&) { ++rejected; }
        try { registry.Remove<Name>(e); } catch (const std::logic_error&) { ++rejected; }
        try { registry.Destroy(e); } catch (const std::logic_error&) { ++rejected; }
        try { registry.Create(); } catch (const std::logic_error&) { ++rejected; }
        registry.ForEach<Name>([](Entity, Name& nested) { nested.value += " nested"; });
    });
    CHECK15(rejected == 4 && registry.TryGet<Name>(entity)->value == "field update nested");
    try { registry.ForEachEntity([](Entity) { throw std::runtime_error("callback"); }); }
    catch (const std::runtime_error&) {}
    registry.Add<Aligned>(entity);
    registry.Remove<Name>(entity);
    registry.Remove<Aligned>(entity);
    CHECK15(registry.IsAlive(entity) && !registry.Has<Name>(entity));
    return true;
}
}
bool Stage15Checks(int& assertions)
{
    if (!ChunkBoundariesAndCopy(assertions)) return false;
    CHECK15(Tracked::alive == 0);
    return RandomizedModel(assertions) && TraversalContract(assertions);
}
