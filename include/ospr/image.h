#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ospr {

// pixels is an OSP_FB_SRGBA framebuffer: width*height packed RGBA bytes whose
// first row is the *bottom* of the image, so this flips vertically on the way
// out to PNG. Throws std::runtime_error if the file cannot be written.
void write_png_rgba(
    const std::string& path, int width, int height, const uint32_t* pixels);

// A decoded 8-bit RGBA image. rows is height * width * 4 bytes with the *first*
// row at the bottom, matching the origin OSPRay's texture2d expects, so the
// decoder flips the top-first image data on the way in.
struct Image
{
    int width{0};
    int height{0};
    std::vector<uint8_t> rgba;
};

// Reads a PNG/JPEG/etc. via stb_image, forcing 4 channels. Throws
// std::runtime_error naming the file if it cannot be decoded.
Image read_image(const std::string& path);

} // namespace ospr
