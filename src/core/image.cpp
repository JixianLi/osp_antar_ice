#include "ospr/image.h"

#include <stdexcept>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace ospr {

void write_png_rgba(
    const std::string& path, int width, int height, const uint32_t* pixels)
{
    std::vector<uint32_t> flipped(static_cast<std::size_t>(width) * height);
    for (int row = 0; row < height; ++row) {
        const uint32_t* source = pixels + static_cast<std::size_t>(height - 1 - row) * width;
        std::copy(source, source + width, flipped.begin() + static_cast<std::size_t>(row) * width);
    }

    const int stride = width * 4;
    if (!stbi_write_png(path.c_str(), width, height, 4, flipped.data(), stride))
        throw std::runtime_error("failed to write " + path);
}

} // namespace ospr
