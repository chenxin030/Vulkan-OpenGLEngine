#include "scene/Image.h"

#include <cstddef>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#include <stb_image.h>

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
