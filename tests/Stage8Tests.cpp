#include "scene/Environment.h"
#include "scene/Scene.h"
#include "scene/RenderSnapshot.h"
#include "scene/SceneSerializer.h"
#include "scene/SceneUpdater.h"
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
constexpr float pi = 3.14159265358979323846f;
FloatMip Constant(glm::vec3 value)
{
    FloatMip image{8, 4, std::vector<float>(8 * 4 * 4, 1)};
    for (size_t i = 0; i < image.pixels.size(); i += 4)
    {
        image.pixels[i] = value.x;
        image.pixels[i + 1] = value.y;
        image.pixels[i + 2] = value.z;
    }
    return image;
}
void Hdr(const std::filesystem::path& path, uint8_t red = 128)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    const std::array<uint8_t, 4> pixel{red, 64, 32, 130};
    for (int i = 0; i < 8; ++i)
        file.write(reinterpret_cast<const char*>(pixel.data()), 4);
}
bool ConstantTexture(const FloatTexture& t, glm::vec3 expected, float tolerance)
{
    for (const auto& mip : t.mips)
        for (size_t i = 0; i < mip.pixels.size(); i += 4)
            if (glm::length(glm::vec3(mip.pixels[i], mip.pixels[i + 1], mip.pixels[i + 2]) - expected) >
                tolerance)
                return false;
    return true;
}
// 独立均匀半球 quadrature，不使用烘焙器的 Hammersley/GGX 采样实现。
glm::vec2 ReferenceBrdf(float NoV, float r)
{
    glm::vec3 v(std::sqrt(1 - NoV * NoV), 0, NoV);
    glm::vec2 sum(0);
    const float alpha = r * r, a2 = alpha * alpha;
    constexpr int nz = 128, np = 256;
    for (int z = 0; z < nz; ++z)
        for (int p = 0; p < np; ++p)
        {
            float NoL = (z + 0.5f) / nz, phi = 2 * pi * (p + 0.5f) / np;
            glm::vec3 l(std::sqrt(1 - NoL * NoL) * std::cos(phi), std::sqrt(1 - NoL * NoL) * std::sin(phi),
                        NoL);
            glm::vec3 h = glm::normalize(v + l);
            float VoH = glm::dot(v, h);
            const float denominator = h.z * h.z * (a2 - 1) + 1;
            float D = a2 / (pi * denominator * denominator);
            float visibility = 0.5f / (NoL * std::sqrt(NoV * NoV * (1 - a2) + a2) +
                                       NoV * std::sqrt(NoL * NoL * (1 - a2) + a2));
            float Fc = std::pow(1 - VoH, 5);
            sum += glm::vec2(1 - Fc, Fc) * (D * visibility * NoL);
        }
    return sum * (2 * pi / (nz * np));
}
} // namespace

bool Stage8Checks(int& assertions)
{
#define VERIFY(...)                                                                                          \
    do                                                                                                       \
    {                                                                                                        \
        ++assertions;                                                                                        \
        if (!(__VA_ARGS__))                                                                                  \
        {                                                                                                    \
            std::cerr << "Stage8 check failed at " << __LINE__ << ": " #__VA_ARGS__ << '\n';                 \
            return false;                                                                                    \
        }                                                                                                    \
    } while (false)
    const std::array<glm::vec3, 6> axes{glm::vec3(1, 0, 0), {-1, 0, 0}, {0, 1, 0},
                                        {0, -1, 0},         {0, 0, 1},  {0, 0, -1}};
    for (uint32_t face = 0; face < 6; ++face)
        VERIFY(glm::length(CubeDirection(face, 0.5f, 0.5f) - axes[face]) < 1e-6f);
    // 相邻面共享边的世界方向；不靠相机矩阵翻转来修正 cubemap。
    VERIFY(glm::length(CubeDirection(0, 0, 0.3f) - CubeDirection(4, 1, 0.3f)) < 1e-6f);
    VERIFY(glm::length(CubeDirection(0, 1, 0.3f) - CubeDirection(5, 0, 0.3f)) < 1e-6f);
    auto constant = Constant({2, 1, 0.5f});
    VERIFY(glm::length(SamplePanorama(constant, {-1, 0, 0.001f}) - glm::vec3(2, 1, 0.5f)) < 1e-6f);
    VERIFY(glm::length(SamplePanorama(constant, {0, 1, 0}) - glm::vec3(2, 1, 0.5f)) < 1e-6f);
    const EnvironmentBakeOptions small{8, 4, 8, 128};
    auto baked = BakeEnvironment(constant, small);
    VERIFY(baked.radiance.layers == 6 && baked.prefiltered.mips.size() == 4);
    VERIFY(baked.prefiltered.mips.back().width == 1);
    VERIFY(ConstantTexture(baked.radiance, {2, 1, 0.5f}, 1e-5f));
    VERIFY(ConstantTexture(baked.irradiance, glm::vec3(2, 1, 0.5f) * pi, 2e-4f));
    VERIFY(ConstantTexture(baked.prefiltered, {2, 1, 0.5f}, 2e-4f));
    auto black = BakeEnvironment(Constant({0, 0, 0}), small);
    VERIFY(ConstantTexture(black.irradiance, {0, 0, 0}, 0));
    VERIFY(ConstantTexture(black.prefiltered, {0, 0, 0}, 0));
    FloatMip hotspot{32, 16, std::vector<float>(32 * 16 * 4, 1)};
    for (uint32_t y = 0; y < hotspot.height; ++y)
        for (uint32_t x = 0; x < hotspot.width; ++x)
        {
            const float theta = pi * (y + 0.5f) / hotspot.height,
                        phi = 2 * pi * ((x + 0.5f) / hotspot.width - 0.5f);
            const float value = 0.05f + 20 * std::pow(std::max(std::sin(theta) * std::sin(phi), 0.0f), 32.0f);
            const size_t i = (size_t(y) * hotspot.width + x) * 4;
            hotspot.pixels[i] = hotspot.pixels[i + 1] = hotspot.pixels[i + 2] = value;
        }
    const auto filtered = BakeEnvironment(hotspot, {16, 4, 8, 256});
    const auto& mirror = filtered.prefiltered.mips.front();
    const size_t mirrorCenter =
        ((size_t(4) * mirror.height + mirror.height / 2) * mirror.width + mirror.width / 2) * 4;
    const float roughCenter = filtered.prefiltered.mips.back().pixels[4 * 4];
    VERIFY(mirror.pixels[mirrorCenter] > 5 && roughCenter < mirror.pixels[mirrorCenter] * 0.25f);
    auto lut = BakeBrdfLut({8, 4, 16, 2048});
    VERIFY(std::all_of(lut.mips[0].pixels.begin(), lut.mips[0].pixels.end(),
                       [](float value) { return std::isfinite(value) && value >= 0 && value <= 1.01f; }));
    // 中等/高粗糙度下与另一种积分方法比较，避免测试只镜像实现。
    for (auto xy : {glm::ivec2(9, 9), glm::ivec2(4, 13), glm::ivec2(14, 11)})
    {
        const auto reference = ReferenceBrdf((xy.x + 0.5f) / 16, (xy.y + 0.5f) / 16);
        size_t i = (size_t(xy.y) * 16 + xy.x) * 4;
        VERIFY(glm::length(glm::vec2(lut.mips[0].pixels[i], lut.mips[0].pixels[i + 1]) - reference) < 0.025f);
    }
    auto invalid = constant;
    invalid.pixels[0] = std::numeric_limits<float>::infinity();
    bool rejected = false;
    try
    {
        BakeEnvironment(invalid, small);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    VERIFY(rejected);
    rejected = false;
    try
    {
        BakeEnvironment(constant, {7, 4, 8, 128});
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    VERIFY(rejected);
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("Rebuild-stage8-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const auto source = folder / "environment.hdr", cache = folder / "cache";
    Hdr(source);
    const auto first = LoadEnvironment(source, cache, small);
    VERIFY(!first.environmentCacheHit && !first.cacheWriteFailed);
    VERIFY(ConstantTexture(first.data->radiance, {2, 1, 0.5f}, 1e-6f));
    const auto second = LoadEnvironment(source, cache, small);
    VERIFY(second.environmentCacheHit && !second.cacheWriteFailed);
    VERIFY(first.data->brdfLut == second.data->brdfLut);
    VERIFY(first.data->prefiltered.mips[1].pixels == second.data->prefiltered.mips[1].pixels);
    // 缓存像素仍是有限数值的损坏，也必须由 payload digest 检出。
    {
        std::fstream file(first.environmentCachePath, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-5, std::ios::end);
        const char bad = 37;
        file.write(&bad, 1);
    }
    const auto repaired = LoadEnvironment(source, cache, small);
    VERIFY(!repaired.environmentCacheHit && !repaired.cacheWriteFailed);
    VERIFY(LoadEnvironment(source, cache, small).environmentCacheHit);
    Hdr(source, 64);
    const auto changed = LoadEnvironment(source, cache, small);
    VERIFY(!changed.environmentCacheHit && changed.environmentCachePath != first.environmentCachePath);
    VERIFY(ConstantTexture(changed.data->radiance, {1, 1, 0.5f}, 1e-6f));
    const auto different = LoadEnvironment(source, cache, {8, 4, 8, 64});
    VERIFY(!different.environmentCacheHit && different.environmentCachePath != changed.environmentCachePath);
    const auto blocked = folder / "blocked";
    {
        std::ofstream file(blocked);
        file << "file, not directory";
    }
    const auto noWrite = LoadEnvironment(source, blocked, small);
    VERIFY(noWrite.cacheWriteFailed && ConstantTexture(noWrite.data->radiance, {1, 1, 0.5f}, 1e-6f));
    const auto unicodeSource = folder / std::filesystem::path(u8"天空.hdr");
    std::filesystem::copy_file(source, unicodeSource);
    VERIFY(LoadEnvironment(unicodeSource, cache, small).environmentCacheHit);
    Scene scene;
    scene.environment = {source.generic_string(), true, true, 1.75f};
    scene.environmentData = changed.data;
    scene.directLighting = false;
    Scene play = scene;
    VERIFY(play.environmentData == scene.environmentData);
    play.environment.intensity = 4;
    VERIFY(scene.environment.intensity == 1.75f);
    auto snapshot = BuildRenderSnapshot(scene);
    VERIFY(snapshot.environmentData == changed.data && snapshot.environment.intensity == 1.75f);
    VERIFY(snapshot.lightingTerms.x == 0 && snapshot.lightingTerms.y == 1);
    const auto scenePath = folder / "test.scene.json";
    SceneSerializer::Save(scene, scenePath);
    auto loaded = SceneSerializer::Load(scenePath);
    VERIFY(loaded.environment.enabled && loaded.environment.background &&
           loaded.environment.intensity == 1.75f);
    VERIFY(loaded.environmentData && loaded.directLighting);
    nlohmann::json json;
    {
        std::ifstream file(scenePath);
        file >> json;
    }
    VERIFY(json["version"] == 3 && json["environment"]["path"] == "environment.hdr");
    VERIFY(!json["environment"].contains("fingerprint"));
    Hdr(source, 192);
    loaded = SceneSerializer::Load(scenePath);
    VERIFY(ConstantTexture(loaded.environmentData->radiance, {3, 1, 0.5f}, 1e-6f));
    const auto before = loaded.environmentData;
    {
        std::ofstream file(source);
        file << "broken HDR";
    }
    rejected = false;
    try
    {
        loaded = SceneSerializer::Load(scenePath);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    VERIFY(rejected && loaded.environmentData == before);
    json.erase("environment");
    {
        std::ofstream file(scenePath);
        file << json;
    }
    loaded = SceneSerializer::Load(scenePath);
    VERIFY(!loaded.environment.enabled && !loaded.environment.background && !loaded.environmentData);
    json["environment"] = {
        {"path", "environment.hdr"}, {"enabled", true}, {"background", false}, {"intensity", -1}};
    {
        std::ofstream file(scenePath);
        file << json;
    }
    rejected = false;
    try
    {
        SceneSerializer::Load(scenePath);
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    VERIFY(rejected);
    VERIFY(FallbackEnvironment() == FallbackEnvironment());
#undef VERIFY
    return true;
}
