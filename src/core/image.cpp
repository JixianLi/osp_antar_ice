#include "ospr/image.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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

Image read_image(const std::string& path)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* decoded = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (decoded == nullptr)
        throw std::runtime_error("cannot decode image " + path + ": " + stbi_failure_reason());

    Image image;
    image.width = width;
    image.height = height;
    image.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    // stb_image's first row is the top; OSPRay's texture origin is the bottom,
    // so flip vertically here rather than through the texture coordinates.
    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
    for (int row = 0; row < height; ++row) {
        const unsigned char* source = decoded + static_cast<std::size_t>(height - 1 - row) * row_bytes;
        std::copy(source, source + row_bytes, image.rgba.begin() + static_cast<std::size_t>(row) * row_bytes);
    }
    stbi_image_free(decoded);
    return image;
}

} // namespace ospr
