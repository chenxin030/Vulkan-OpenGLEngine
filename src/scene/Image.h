#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
