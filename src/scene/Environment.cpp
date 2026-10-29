#include "scene/Environment.h"
#include "scene/ContentFingerprint.h"
#include <stb_image.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>

namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr uint32_t bakeVersion = 1;
constexpr size_t maxFileBytes = 256u * 1024u * 1024u;

void ValidateOptions(const EnvironmentBakeOptions& o)
{
    if (!std::has_single_bit(o.cubeSize) || o.cubeSize > 512 || !std::has_single_bit(o.irradianceSize) ||
        o.irradianceSize > 64 || !std::has_single_bit(o.lutSize) || o.lutSize > 256 || o.samples == 0 ||
        o.samples > 4096)
        throw std::runtime_error("invalid environment bake dimensions/sample count");
}
void ValidatePanorama(const FloatMip& p)
{
    if (!p.width || !p.height || uint64_t(p.width) * p.height > 16u * 1024u * 1024u ||
        p.pixels.size() != uint64_t(p.width) * p.height * 4)
        throw std::runtime_error("invalid HDR panorama dimensions/pixels");
    for (float value : p.pixels)
        if (!std::isfinite(value) || value < 0 || value > 1e10f)
            throw std::runtime_error("HDR contains invalid or excessive radiance");
}
std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot read environment/cache file: " + path.string());
    const auto size = file.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > maxFileBytes)
        throw std::runtime_error("invalid environment/cache file size");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("incomplete environment/cache read");
    return bytes;
}
std::string Hash(std::span<const uint8_t> bytes)
{
    ContentFingerprint hash;
    hash.Update(bytes);
    return hash.Hex();
}
FloatTexture Allocate(uint32_t size, uint32_t layers, uint32_t levels = 1)
{
    FloatTexture t;
    t.layers = layers;
    for (uint32_t mip = 0; mip < levels; ++mip)
    {
        uint32_t dim = std::max(1u, size >> mip);
        t.mips.push_back({dim, dim, std::vector<float>(size_t(dim) * dim * layers * 4, 0)});
    }
    return t;
}
void Store(FloatMip& mip, uint32_t face, uint32_t x, uint32_t y, glm::vec3 value)
{
    const size_t i = ((size_t(face) * mip.height + y) * mip.width + x) * 4;
    mip.pixels[i] = value.x;
    mip.pixels[i + 1] = value.y;
    mip.pixels[i + 2] = value.z;
    mip.pixels[i + 3] = 1;
}
float RadicalInverse(uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return float(bits) * 2.3283064365386963e-10f;
}
glm::vec3 World(glm::vec3 local, glm::vec3 n)
{
    const glm::vec3 up = std::abs(n.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, n));
    return tangent * local.x + glm::cross(n, tangent) * local.y + n * local.z;
}
glm::vec3 GgxHalf(float x, float y, float roughness)
{
    const float alpha = roughness * roughness, a2 = alpha * alpha;
    const float cosTheta = std::sqrt((1 - y) / (1 + (a2 - 1) * y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1 - cosTheta * cosTheta));
    return {std::cos(2 * pi * x) * sinTheta, std::sin(2 * pi * x) * sinTheta, cosTheta};
}

// 缓存只使用 little-endian u32 / IEEE float。尺寸由烘焙配置决定，不信任文件给出的分配大小。
void U32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(uint8_t(value >> (i * 8)));
}
uint32_t U32(std::span<const uint8_t> bytes, size_t& offset)
{
    if (offset + 4 > bytes.size())
        throw std::runtime_error("truncated IBL cache");
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= uint32_t(bytes[offset++]) << (i * 8);
    return value;
}
void Pack(std::vector<uint8_t>& bytes, const FloatTexture& t)
{
    U32(bytes, t.layers);
    U32(bytes, uint32_t(t.mips.size()));
    for (const auto& m : t.mips)
    {
        U32(bytes, m.width);
        U32(bytes, m.height);
        for (float f : m.pixels)
            U32(bytes, std::bit_cast<uint32_t>(f));
    }
}
void Unpack(std::span<const uint8_t> bytes, size_t& offset, FloatTexture& expected)
{
    if (U32(bytes, offset) != expected.layers || U32(bytes, offset) != expected.mips.size())
        throw std::runtime_error("IBL cache layout mismatch");
    for (auto& m : expected.mips)
    {
        if (U32(bytes, offset) != m.width || U32(bytes, offset) != m.height)
            throw std::runtime_error("IBL cache mip dimensions mismatch");
        for (float& f : m.pixels)
        {
            f = std::bit_cast<float>(U32(bytes, offset));
            if (!std::isfinite(f) || f < 0 || f > 1e12f)
                throw std::runtime_error("invalid IBL cache pixel");
        }
    }
}
bool ReadCache(const std::filesystem::path& path, const std::string& key,
               std::vector<FloatTexture*>& textures)
{
    try
    {
        const auto bytes = ReadBytes(path);
        constexpr size_t header = 4 + 64 + 64;
        if (bytes.size() < header || std::memcmp(bytes.data(), "IBL1", 4) != 0 ||
            std::memcmp(bytes.data() + 4, key.data(), 64) != 0 ||
            Hash(std::span(bytes).subspan(header)) !=
                std::string(reinterpret_cast<const char*>(bytes.data() + 68), 64))
            return false;
        size_t offset = header;
        for (auto* t : textures)
            Unpack(bytes, offset, *t);
        return offset == bytes.size();
    }
    catch (const std::exception&)
    {
        return false;
    } // 无效缓存可重建，不使场景加载失败。
}
bool WriteCache(const std::filesystem::path& path, const std::string& key,
                const std::vector<const FloatTexture*>& textures)
{
    try
    {
        std::vector<uint8_t> payload;
        for (auto* t : textures)
            Pack(payload, *t);
        const std::string digest = Hash(payload);
        std::filesystem::create_directories(path.parent_path());
        auto temporary = path;
        temporary += ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write("IBL1", 4);
        file.write(key.data(), 64);
        file.write(digest.data(), 64);
        file.write(reinterpret_cast<const char*>(payload.data()), std::streamsize(payload.size()));
        file.close();
        if (!file)
            return false;
        // 完整写入后替换；缓存文件不是源资产。失败也能继续使用内存里的计算结果。
        std::error_code error;
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
        return !error;
    }
    catch (const std::exception&)
    {
        return false;
    }
}
std::string Recipe(const EnvironmentBakeOptions& o, bool lut)
{
    std::vector<uint8_t> bytes;
    U32(bytes, bakeVersion);
    U32(bytes, o.samples);
    U32(bytes, lut ? o.lutSize : o.cubeSize);
    if (!lut)
        U32(bytes, o.irradianceSize);
    return Hash(bytes);
}
EnvironmentData EmptyEnvironment(const EnvironmentBakeOptions& o)
{
    EnvironmentData data;
    data.radiance = Allocate(o.cubeSize, 6);
    data.irradiance = Allocate(o.irradianceSize, 6);
    data.prefiltered = Allocate(o.cubeSize, 6, std::bit_width(o.cubeSize));
    return data;
}
} // namespace

glm::vec3 CubeDirection(uint32_t face, float u, float v)
{
    // u/v 是纹理方向，v 随行号增加。两个 API 使用同一套 cube 采样面约定。
    const float s = 2 * u - 1, t = 2 * v - 1;
    switch (face)
    {
    case 0:
        return glm::normalize(glm::vec3(1, -t, -s));
    case 1:
        return glm::normalize(glm::vec3(-1, -t, s));
    case 2:
        return glm::normalize(glm::vec3(s, 1, t));
    case 3:
        return glm::normalize(glm::vec3(s, -1, -t));
    case 4:
        return glm::normalize(glm::vec3(s, -t, 1));
    case 5:
        return glm::normalize(glm::vec3(-s, -t, -1));
    default:
        throw std::runtime_error("invalid cubemap face");
    }
}

glm::vec3 SamplePanorama(const FloatMip& p, glm::vec3 direction)
{
    direction = glm::normalize(direction);
    const float u = std::atan2(direction.z, direction.x) / (2 * pi) + 0.5f;
    const float v = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / pi;
    const float x = u * p.width - 0.5f, y = v * p.height - 0.5f;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    auto at = [&](int ix, int iy) {
        ix = (ix % int(p.width) + int(p.width)) % int(p.width);
        iy = std::clamp(iy, 0, int(p.height) - 1);
        const size_t i = (size_t(iy) * p.width + ix) * 4;
        return glm::vec3(p.pixels[i], p.pixels[i + 1], p.pixels[i + 2]);
    };
    return glm::mix(glm::mix(at(x0, y0), at(x0 + 1, y0), x - x0),
                    glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), x - x0), y - y0);
}

EnvironmentData BakeEnvironment(const FloatMip& panorama, const EnvironmentBakeOptions& o)
{
    ValidateOptions(o);
    ValidatePanorama(panorama);
    EnvironmentData data = EmptyEnvironment(o);
    // 源图像的普通 mip 用于采样足迹过滤；与输出按粗糙度烘焙的 mip 是两种不同用途。
    std::vector<FloatMip> sourceMips{panorama};
    while (sourceMips.back().width > 1 || sourceMips.back().height > 1)
    {
        const auto& src = sourceMips.back();
        FloatMip dst{std::max(1u, src.width / 2), std::max(1u, src.height / 2), {}};
        dst.pixels.resize(size_t(dst.width) * dst.height * 4);
        for (uint32_t y = 0; y < dst.height; ++y)
            for (uint32_t x = 0; x < dst.width; ++x)
            {
                glm::vec3 sum(0);
                for (uint32_t dy = 0; dy < 2; ++dy)
                    for (uint32_t dx = 0; dx < 2; ++dx)
                    {
                        size_t i = (size_t(std::min(src.height - 1, y * 2 + dy)) * src.width +
                                    std::min(src.width - 1, x * 2 + dx)) *
                                   4;
                        sum += glm::vec3(src.pixels[i], src.pixels[i + 1], src.pixels[i + 2]);
                    }
                Store(dst, 0, x, y, sum * 0.25f);
            }
        sourceMips.push_back(std::move(dst));
    }
    std::vector<glm::vec3> diffuseSamples;
    diffuseSamples.reserve(o.samples);
    for (uint32_t i = 0; i < o.samples; ++i)
    {
        const float x = float(i) / o.samples, y = RadicalInverse(i);
        diffuseSamples.emplace_back(std::cos(2 * pi * x) * std::sqrt(y), std::sin(2 * pi * x) * std::sqrt(y),
                                    std::sqrt(1 - y));
    }
    for (uint32_t face = 0; face < 6; ++face)
    {
        auto& r = data.radiance.mips[0];
        for (uint32_t y = 0; y < r.height; ++y)
            for (uint32_t x = 0; x < r.width; ++x)
                Store(r, face, x, y,
                      SamplePanorama(panorama,
                                     CubeDirection(face, (x + 0.5f) / r.width, (y + 0.5f) / r.height)));
        auto& e = data.irradiance.mips[0];
        for (uint32_t y = 0; y < e.height; ++y)
            for (uint32_t x = 0; x < e.width; ++x)
            {
                glm::vec3 n = CubeDirection(face, (x + 0.5f) / e.width, (y + 0.5f) / e.height), sum(0);
                // cosine-weighted PDF=NoL/pi，积分的 cos 与 PDF 抵消，所以 E=pi*平均 radiance。
                for (auto local : diffuseSamples)
                    sum += SamplePanorama(panorama, World(local, n));
                Store(e, face, x, y, sum * (pi / o.samples));
            }
    }
    for (uint32_t mip = 0; mip < data.prefiltered.mips.size(); ++mip)
    {
        auto& dst = data.prefiltered.mips[mip];
        const float roughness =
            data.prefiltered.mips.size() > 1 ? float(mip) / (data.prefiltered.mips.size() - 1) : 0;
        std::vector<glm::vec3> halfSamples;
        if (mip)
            for (uint32_t i = 0; i < o.samples; ++i)
                halfSamples.push_back(GgxHalf(float(i) / o.samples, RadicalInverse(i), roughness));
        for (uint32_t face = 0; face < 6; ++face)
            for (uint32_t y = 0; y < dst.height; ++y)
                for (uint32_t x = 0; x < dst.width; ++x)
                {
                    const glm::vec3 n = CubeDirection(face, (x + 0.5f) / dst.width, (y + 0.5f) / dst.height);
                    if (!mip)
                    {
                        Store(dst, face, x, y, SamplePanorama(panorama, n));
                        continue;
                    }
                    glm::vec3 sum(0);
                    float weight = 0;
                    for (auto local : halfSamples)
                    {
                        const glm::vec3 h = World(local, n), l = glm::normalize(2 * glm::dot(n, h) * h - n);
                        const float NoL = std::max(glm::dot(n, l), 0.0f);
                        if (NoL <= 0)
                            continue;
                        const float NoH = std::max(local.z, 1e-6f), a = roughness * roughness, a2 = a * a;
                        const float d = NoH * NoH * (a2 - 1) + 1;
                        const float pdf = a2 / (pi * d * d) * 0.25f; // V=N，NoH/(4*VoH)=1/4。
                        const float solidSample = 1 / (o.samples * std::max(pdf, 1e-8f));
                        const float solidPixel = 2 * pi * pi *
                                                 std::max(std::sqrt(std::max(0.0f, 1 - l.y * l.y)), 0.001f) /
                                                 (panorama.width * panorama.height);
                        const float lod = std::clamp(0.5f * std::log2(solidSample / solidPixel), 0.0f,
                                                     float(sourceMips.size() - 1));
                        const uint32_t lo = uint32_t(lod),
                                       hi = std::min(lo + 1, uint32_t(sourceMips.size() - 1));
                        sum += glm::mix(SamplePanorama(sourceMips[lo], l), SamplePanorama(sourceMips[hi], l),
                                        lod - lo) *
                               NoL;
                        weight += NoL;
                    }
                    Store(dst, face, x, y, weight > 0 ? sum / weight : SamplePanorama(panorama, n));
                }
    }
    return data;
}

FloatTexture BakeBrdfLut(const EnvironmentBakeOptions& o)
{
    ValidateOptions(o);
    auto lut = Allocate(o.lutSize, 1);
    for (uint32_t y = 0; y < o.lutSize; ++y)
    {
        const float roughness = std::max((y + 0.5f) / o.lutSize, 0.045f), a = roughness * roughness,
                    a2 = a * a;
        std::vector<glm::vec3> halfSamples;
        for (uint32_t i = 0; i < o.samples; ++i)
            halfSamples.push_back(GgxHalf(float(i) / o.samples, RadicalInverse(i), roughness));
        for (uint32_t x = 0; x < o.lutSize; ++x)
        {
            const float NoV = (x + 0.5f) / o.lutSize;
            const glm::vec3 v(std::sqrt(1 - NoV * NoV), 0, NoV);
            glm::vec2 sum(0);
            for (auto h : halfSamples)
            {
                const float VoH = std::clamp(glm::dot(v, h), 0.0f, 1.0f);
                const glm::vec3 l = 2 * VoH * h - v;
                const float NoL = std::max(l.z, 0.0f);
                if (NoL <= 0 || VoH <= 0)
                    continue;
                // Filament 的 height-correlated Smith visibility。积分使用完整的 PDF 权重。
                const float visibility = 0.5f / std::max(NoL * std::sqrt(NoV * NoV * (1 - a2) + a2) +
                                                             NoV * std::sqrt(NoL * NoL * (1 - a2) + a2),
                                                         1e-8f);
                const float weight = 4 * visibility * NoL * VoH / std::max(h.z, 1e-6f);
                const float Fc = std::pow(1 - VoH, 5.0f);
                sum += glm::vec2((1 - Fc) * weight, Fc * weight);
            }
            Store(lut.mips[0], 0, x, y, {sum.x / o.samples, sum.y / o.samples, 0});
        }
    }
    return lut;
}

std::filesystem::path EnvironmentCacheDirectory()
{
    return std::filesystem::path(IBL_CACHE_DIR);
}

EnvironmentLoadResult LoadEnvironment(const std::filesystem::path& source,
                                      const std::filesystem::path& cacheDirectory,
                                      const EnvironmentBakeOptions& o)
{
    const auto started = std::chrono::steady_clock::now();
    ValidateOptions(o);
    if (source.extension() != L".hdr")
        throw std::runtime_error("environment requires a .hdr panorama");
    const auto bytes = ReadBytes(source);
    // 哈希与解码同一批字节，避免先 hash、后读文件期间内容改变。
    if (!stbi_is_hdr_from_memory(bytes.data(), int(bytes.size())))
        throw std::runtime_error("environment is not a Radiance HDR image");
    const std::string key = Hash(bytes) + Recipe(o, false);
    const std::string environmentKey = Hash({reinterpret_cast<const uint8_t*>(key.data()), key.size()});
    const std::string lutKey = Recipe(o, true);

    EnvironmentLoadResult result;
    result.environmentCachePath = cacheDirectory / ("environment-" + environmentKey + ".ibl");
    result.lutCachePath = cacheDirectory / ("brdf-" + lutKey + ".ibl");

    auto data = std::make_shared<EnvironmentData>(EmptyEnvironment(o));
    std::vector<FloatTexture*> environmentTextures{&data->radiance, &data->irradiance, &data->prefiltered};
    result.environmentCacheHit = ReadCache(result.environmentCachePath, environmentKey, environmentTextures);
    if (!result.environmentCacheHit)
    {
        int w = 0, h = 0, c = 0;
        if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c) || w <= 0 || h <= 0 ||
            uint64_t(w) * h > 16u * 1024u * 1024u)
            throw std::runtime_error("invalid or excessive HDR dimensions");
        float* pixels = stbi_loadf_from_memory(bytes.data(), int(bytes.size()), &w, &h, &c, 4);
        if (!pixels)
            throw std::runtime_error("HDR decode failed");
        std::unique_ptr<float, decltype(&stbi_image_free)> owned(pixels, stbi_image_free);
        FloatMip panorama{uint32_t(w), uint32_t(h), std::vector<float>(pixels, pixels + size_t(w) * h * 4)};
        *data = BakeEnvironment(panorama, o);
        result.cacheWriteFailed = !WriteCache(result.environmentCachePath, environmentKey,
                                              {&data->radiance, &data->irradiance, &data->prefiltered});
    }
    // LUT 的内存复用范围是进程，不属于某张 HDR。环境切换/重载不重复计算它。
    static std::unordered_map<std::string, std::shared_ptr<const FloatTexture>> luts;
    auto found = luts.find(lutKey);
    if (found != luts.end())
    {
        data->brdfLut = found->second;
        result.lutMemoryHit = true;
    }
    else
    {
        auto lut = std::make_shared<FloatTexture>(Allocate(o.lutSize, 1));
        std::vector<FloatTexture*> texture{lut.get()};
        result.lutCacheHit = ReadCache(result.lutCachePath, lutKey, texture);
        if (!result.lutCacheHit)
        {
            *lut = BakeBrdfLut(o);
            result.cacheWriteFailed =
                !WriteCache(result.lutCachePath, lutKey, {lut.get()}) || result.cacheWriteFailed;
        }
        luts.emplace(lutKey, lut);
        data->brdfLut = std::move(lut);
    }
    result.data = std::move(data);
    std::cout << "[IBL] environment " << (result.environmentCacheHit ? "cache hit" : "baked") << ", BRDF "
              << (result.lutMemoryHit  ? "memory reuse"
                  : result.lutCacheHit ? "cache hit"
                                       : "baked")
              << ", "
              << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                       started)
                     .count()
              << " ms" << std::endl;
    if (result.cacheWriteFailed)
        std::cout << "[IBL] cache write unavailable; using in-memory result\n";
    return result;
}

std::shared_ptr<const EnvironmentData> FallbackEnvironment()
{
    static const auto fallback = [] {
        auto data = std::make_shared<EnvironmentData>();
        data->radiance = Allocate(1, 6);
        data->irradiance = Allocate(1, 6);
        data->prefiltered = Allocate(1, 6);
        data->brdfLut = std::make_shared<FloatTexture>(Allocate(1, 1));
        return data;
    }();
    return fallback;
}

void ValidateEnvironmentSettings(const EnvironmentSettings& s)
{
    if (!std::isfinite(s.intensity) || s.intensity < 0 || s.intensity > 10000 ||
        s.sourcePath.size() > 32768 || s.sourcePath.find('\0') != std::string::npos ||
        (s.sourcePath.empty() && (s.enabled || s.background)))
        throw std::runtime_error("invalid environment settings");
}
