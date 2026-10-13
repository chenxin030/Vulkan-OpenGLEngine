#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// 环境是场景级光照，不是模型材质。这里只描述 CPU 数据，不含任何图形 API 句柄。
struct EnvironmentSettings
{
    std::string sourcePath;
    bool enabled = false;
    bool background = false;
    float intensity = 1.0f;
};

struct FloatMip
{
    uint32_t width = 0, height = 0;
    // RGBA float；层优先，其次行、列。cubemap 的层顺序为 +X,-X,+Y,-Y,+Z,-Z。
    std::vector<float> pixels;
};

struct FloatTexture
{
    uint32_t layers = 1;
    std::vector<FloatMip> mips;
};

struct EnvironmentData
{
    FloatTexture radiance;
    FloatTexture irradiance;                     // 存 E，而不是 E/pi；shader 的 Lambert 项再除 pi。
    FloatTexture prefiltered;                    // mip 0 是镜面；其余层分别进行 GGX 积分，不是普通缩小。
    std::shared_ptr<const FloatTexture> brdfLut; // RG = A,B，重建为 F0*A+B。
};

struct EnvironmentBakeOptions
{
    uint32_t cubeSize = 128;
    uint32_t irradianceSize = 16;
    uint32_t lutSize = 128;
    uint32_t samples = 512;
};

struct EnvironmentLoadResult
{
    std::shared_ptr<const EnvironmentData> data;
    bool environmentCacheHit = false, lutCacheHit = false, lutMemoryHit = false;
    bool cacheWriteFailed = false;
    std::filesystem::path environmentCachePath, lutCachePath;
};

// 这两项公开是为了检验约定和积分，不要求阅读图片格式解析。
glm::vec3 CubeDirection(uint32_t face, float u, float v);
glm::vec3 SamplePanorama(const FloatMip& panorama, glm::vec3 direction);
EnvironmentData BakeEnvironment(const FloatMip& panorama, const EnvironmentBakeOptions& options);
FloatTexture BakeBrdfLut(const EnvironmentBakeOptions& options);
std::filesystem::path EnvironmentCacheDirectory();
EnvironmentLoadResult LoadEnvironment(
    const std::filesystem::path& source,
    const std::filesystem::path& cacheDirectory = EnvironmentCacheDirectory(),
    const EnvironmentBakeOptions& options = {});
std::shared_ptr<const EnvironmentData> FallbackEnvironment();
void ValidateEnvironmentSettings(const EnvironmentSettings& settings);
