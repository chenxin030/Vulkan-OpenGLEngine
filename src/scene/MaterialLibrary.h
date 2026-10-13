#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// 材质和图片由 MaterialLibrary 统一存储，实体只保存材质索引。
struct SurfaceMaterial
{
    glm::vec4 baseColorFactor{ 1.0f };
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.8f;
    uint32_t baseColorTexture = 0xFFFFFFFFu;
    glm::vec3 emissive{ 0.0f };
};

struct Image
{
    uint32_t width = 0;
    uint32_t height = 0;

    std::vector<uint8_t> pixels;

    bool IsValid() const
    {
        return width > 0 && height > 0 && !pixels.empty();
    }
};

Image LoadImage(const std::string& path);

class MaterialLibrary
{
public:
    MaterialLibrary();
    uint32_t AddMaterial(SurfaceMaterial material);
    uint32_t AddTexture(Image image);
    const SurfaceMaterial& GetMaterial(uint32_t index) const;
    const Image& GetTexture(uint32_t index) const;
    uint32_t MaterialCount() const { return static_cast<uint32_t>(m_materials.size()); }
    uint32_t TextureCount() const { return static_cast<uint32_t>(m_textures.size()); }
    std::uint64_t Identity() const { return m_identity; }
    std::uint64_t Revision() const { return m_revision; }

private:
    std::vector<SurfaceMaterial> m_materials{ SurfaceMaterial{} };
    std::vector<Image> m_textures;
    std::uint64_t m_identity = 0;
    std::uint64_t m_revision = 0;
};
