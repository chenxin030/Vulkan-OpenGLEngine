#include "scene/Registry.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

int g_checks = 0;

#define CHECK(condition)                                                        \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(condition)) {                                                      \
            std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__      \
                      << ": " #condition << '\n';                               \
            return false;                                                        \
        }                                                                        \
    } while (false)

struct Position
{
    int value = 0;
};

struct Velocity
{
    int value = 0;
};

bool EntityLifecycleAndComponentStorage()
{
    Registry registry;
    const Entity first = registry.Create();
    const Entity second = registry.Create();

    registry.Add<Position>(first, Position{ 10 });
    registry.Add<Position>(second, Position{ 20 });
    registry.Add<Velocity>(first, Velocity{ 3 });

    CHECK(registry.EntityCount() == 2);
    CHECK(registry.IsAlive(first));
    CHECK(registry.Has<Position>(first));
    CHECK(registry.TryGet<Position>(first)->value == 10);

    int matchingEntities = 0;
    int sum = 0;
    bool matchedExpectedEntity = true;
    registry.ForEach<Position, Velocity>([&](Entity entity, Position& position, Velocity& velocity) {
        matchedExpectedEntity = matchedExpectedEntity && entity == first;
        sum += position.value + velocity.value;
        ++matchingEntities;
    });
    CHECK(matchedExpectedEntity);
    CHECK(matchingEntities == 1);
    CHECK(sum == 13);

    registry.Remove<Velocity>(first);
    CHECK(!registry.Has<Velocity>(first));

    registry.Destroy(first);
    CHECK(!registry.IsAlive(first));
    CHECK(registry.TryGet<Position>(first) == nullptr);
    CHECK(registry.EntityCount() == 1);

    const Entity replacement = registry.Create();
    CHECK(replacement.index == first.index);
    CHECK(replacement.generation != first.generation);
    CHECK(registry.IsAlive(replacement));
    CHECK(!registry.IsAlive(first));
    return true;
}

bool StaleHandlesCannotDetachReusedEntityComponents()
{
    Registry registry;
    const Entity stale = registry.Create();
    registry.Add<Position>(stale, Position{ 1 });
    registry.Destroy(stale);

    const Entity current = registry.Create();
    registry.Add<Position>(current, Position{ 42 });

    registry.Remove<Position>(stale);
    CHECK(registry.Has<Position>(current));
    CHECK(registry.TryGet<Position>(current)->value == 42);
    return true;
}

bool InvalidEntityOperationsAreRejectedOrIgnored()
{
    Registry registry;
    const Entity dead = registry.Create();
    registry.Destroy(dead);

    bool rejected = false;
    try {
        registry.Add<Position>(dead, Position{ 7 });
    }
    catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);

    // Destroy on a stale handle is intentionally harmless.
    registry.Destroy(dead);
    CHECK(registry.EntityCount() == 0);
    return true;
}

} // namespace

int main()
{
    if (!EntityLifecycleAndComponentStorage() ||
        !StaleHandlesCannotDetachReusedEntityComponents() ||
        !InvalidEntityOperationsAreRejectedOrIgnored()) {
        return EXIT_FAILURE;
    }

    std::cout << "Registry checks passed: " << g_checks << '\n';
    return EXIT_SUCCESS;
}
