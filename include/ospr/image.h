#pragma once

#include <cstdint>
#include <string>

namespace ospr {

// pixels is an OSP_FB_SRGBA framebuffer: width*height packed RGBA bytes whose
// first row is the *bottom* of the image, so this flips vertically on the way
// out to PNG. Throws std::runtime_error if the file cannot be written.
void write_png_rgba(
    const std::string& path, int width, int height, const uint32_t* pixels);

} // namespace ospr
