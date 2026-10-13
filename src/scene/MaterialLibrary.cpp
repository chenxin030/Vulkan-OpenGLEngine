#include "MaterialLibrary.h"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>

// stb_image 的实现集中在这个翻译单元，导入器和后端共用同一份图片解码实现。
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#include <stb_image.h>

namespace {
std::atomic_uint64_t g_nextMaterialLibraryIdentity{ 1 };
}

MaterialLibrary::MaterialLibrary()
    : m_identity(g_nextMaterialLibraryIdentity.fetch_add(1, std::memory_order_relaxed))
{
}

uint32_t MaterialLibrary::AddMaterial(SurfaceMaterial material)
{
    if (material.baseColorTexture != 0xFFFFFFFFu && material.baseColorTexture >= m_textures.size())
        throw std::runtime_error("material references a texture that is not loaded");
    if (m_materials.size() >= UINT32_MAX)
        throw std::runtime_error("MaterialLibrary is out of 32-bit indices");
    const uint32_t index = static_cast<uint32_t>(m_materials.size());
    m_materials.push_back(material);
    ++m_revision;
    return index;
}

uint32_t MaterialLibrary::AddTexture(Image image)
{
    if (!image.IsValid()) throw std::runtime_error("MaterialLibrary::AddTexture requires a valid image");
    if (m_textures.size() >= UINT32_MAX) throw std::runtime_error("MaterialLibrary is out of 32-bit indices");
    const uint32_t index = static_cast<uint32_t>(m_textures.size());
    m_textures.push_back(std::move(image));
    ++m_revision;
    return index;
}

const SurfaceMaterial& MaterialLibrary::GetMaterial(uint32_t index) const
{
    if (index >= m_materials.size()) throw std::runtime_error("MaterialLibrary material index is out of range");
    return m_materials[index];
}

const Image& MaterialLibrary::GetTexture(uint32_t index) const
{
    if (index >= m_textures.size()) throw std::runtime_error("MaterialLibrary texture index is out of range");
    return m_textures[index];
}

Image LoadImage(const std::string& path)
{
    int width = 0;
    int height = 0;
    int channelsInFile = 0;

    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channelsInFile, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        throw std::runtime_error("failed to load image '" + path + "': " +
            (reason != nullptr ? reason : "unknown reason"));
    }

    Image image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);

    stbi_image_free(pixels);
    return image;
}
