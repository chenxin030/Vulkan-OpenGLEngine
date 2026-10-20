#include "transport/TransportWorld.h"
#include "transport/TransportBenchmark.h"
#include "scene/Components.h"
#include "scene/RenderSnapshot.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <limits>
#include <map>

bool Stage14Checks(int& checks)
{
#define CHECK14(condition) do { ++checks; if (!(condition)) { std::cerr << "Stage14 failed: " #condition " at " << __LINE__ << '\n'; return false; } } while (0)
    const auto fails = [](auto&& operation) { try { operation(); return false; } catch (const std::exception&) { return true; } };
    CHECK14(fails([] { TransportWorld world(0); }));
    CHECK14(fails([] { TransportWorld world(100001); }));
    CHECK14(fails([] { TransportWorld world(1, -1); }));
    FixedTransportClock clock;
    CHECK14(clock.Advance(0.5) == 8);
    CHECK14(clock.BacklogSeconds() > 0.36);
    uint32_t ticks = 8;
    while (clock.BacklogSeconds() + 1e-12 >= TransportWorld::Step) ticks += clock.Advance(0);
    CHECK14(ticks == 30);
    CHECK14(clock.BacklogSeconds() < 1e-10);
    CHECK14(fails([&] { clock.Advance(std::numeric_limits<double>::quiet_NaN()); }));
    CHECK14(fails([&] { clock.Advance(-1); }));
    FixedTransportClock aClock, bClock;
    TransportWorld a(64), b(64);
    const auto meshRevision = a.GetScene().meshes.Revision(), materialRevision = a.GetScene().materials.Revision();
    const auto meshIdentity = a.GetScene().meshes.Identity(), materialIdentity = a.GetScene().materials.Identity();
    CHECK14(a.GetScene().registry.EntityCount() == 17);
    CHECK14(a.GetScene().materials.TextureCount() == 4);
    for (uint32_t i = 0; i < 4; ++i) {
        const auto& image = a.GetScene().materials.GetTexture(i);
        CHECK14(image.IsValid() && image.srgb && image.pixels.size() == 64*64*4);
    }
    for (int i = 0; i < 4000; ++i) {
        for (uint32_t n = aClock.Advance(1.0/60); n; --n) a.Tick();
        if (i % 2) for (uint32_t n = bClock.Advance(1.0/30); n; --n) b.Tick();
        if (a.Active() > 64 || a.GetScene().registry.EntityCount() != 17 + a.Active()) return false;
    }
    CHECK14(a.Ticks() == b.Ticks() && a.Ticks() == 4000);
    CHECK14(a.Spawned() == b.Spawned() && a.Arrived() == b.Arrived());
    CHECK14(a.Spawned() > 64 && a.Arrived() > 0 && a.Spawned() - a.Arrived() == a.Active());
    CHECK14(a.GetScene().meshes.Revision() == meshRevision && a.GetScene().materials.Revision() == materialRevision);
    const auto states = [](TransportWorld& world) {
        std::map<uint64_t, glm::vec3> result;
        world.GetScene().registry.ForEach<CargoFlight, Transform>([&](Entity entity, CargoFlight& f, Transform& t) {
            if (world.GetScene().registry.Has<Parent>(entity)) throw std::runtime_error("Cargo should be a root");
            result[f.sequence] = glm::vec3(t.world[3]);
        });
        return result;
    };
    CHECK14(states(a) == states(b));
    const auto snapshot = BuildRenderSnapshot(a.GetScene());
    CHECK14(snapshot.drawItems.size() == 17 + a.Active());
    CHECK14(snapshot.skippedDegenerateDraws == 0);
    a.Reset();
    CHECK14(a.Active() == 0 && a.Ticks() == 0 && a.Spawned() == 0 && a.Arrived() == 0);
    CHECK14(a.GetScene().registry.EntityCount() == 17);
    CHECK14(a.GetScene().meshes.Identity() == meshIdentity && a.GetScene().materials.Identity() == materialIdentity);
    a.Tick(); CHECK14(a.Active() == 16);
    TransportWorld tiny(1, 0.01);
    for (int i=0; i<2000; ++i) tiny.Tick();
    CHECK14(tiny.Active() == 1 && tiny.Arrived() > 0 && tiny.GetScene().registry.EntityCount() == 18);
    CHECK14(Percentile({1,2,3,4,5}, 0.5) == 3 && Percentile({1,2,3,4,5}, 0.99) == 5);
    CHECK14(Percentile({1,2},0) == 1);
    const auto parse = [](std::vector<std::string> args) {
        std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data());
        return ParseTransportOptions(int(argv.size()), argv.data());
    };
    CHECK14(fails([&] { parse({"Rebuild","--benchmark"}); }));
    CHECK14(fails([&] { parse({"Rebuild","--transport","--limit","2.5"}); }));
    CHECK14(fails([&] { parse({"Rebuild","--transport","--seconds","nan"}); }));
    CHECK14(fails([&] { parse({"Rebuild","--transport","--unknown","1"}); }));
    const auto options = parse({"Rebuild","opengl","--transport","--benchmark","--limit","4096","--seconds","0.1"});
    CHECK14(options.enabled && options.benchmark && options.backend == "opengl" && options.limit == 4096);
    auto outputOptions = options;
    outputOptions.output = std::filesystem::path(TEST_SOURCE_DIR) / "build/transport-output-tests";
    TransportFrame frame; frame.frame = 7; frame.active = 32; frame.frameMs = 4;
    frame.gpu = GpuSample{7,1,2,3,4};
    WriteTransportResults(outputOptions, {"opengl","test GPU","test",false,true,false},1,0.1,{frame},"",0);
    nlohmann::json summary;
    std::ifstream(outputOptions.output / "opengl-4096-run1.json") >> summary;
    CHECK14(summary["valid"] == true && summary["gpu_samples"] == 1 && summary["active_max"] == 32);
    CHECK14(summary["gpu_shadow_scene_tone"]["median_ms"] == 6);
    WriteTransportResults(outputOptions, {},2,0.1,{},"lost focus",0);
    std::ifstream(outputOptions.output / "opengl-4096-run2.json") >> summary;
    CHECK14(summary["valid"] == false && summary["frame"].is_null());
    return true;
#undef CHECK14
}
