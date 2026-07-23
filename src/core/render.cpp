#include "ospr/render.h"

#include <stdexcept>
#include <string>

#include "ospr/math.h"
#include "ospr/scene.h"

namespace ospr {

Device::Device(int& argc, const char** argv)
{
    const OSPError error = ospInit(&argc, argv);
    if (error != OSP_NO_ERROR)
        throw std::runtime_error(
            "ospInit failed with code " + std::to_string(static_cast<int>(error)));
}

Device::~Device()
{
    ospShutdown();
}

FrameRenderer::FrameRenderer(
    const Script& script, int width, int height, int samples_per_pixel)
    : width_(width)
    , height_(height)
    , samples_per_pixel_(samples_per_pixel)
    , world_(build_world(script.scene))
    , renderer_("scivis")
    , camera_("perspective")
    , framebuffer_(width, height, OSP_FB_SRGBA, OSP_FB_COLOR | OSP_FB_ACCUM)
    , pixels_(static_cast<std::size_t>(width) * height)
{
    const Vec3& bg = script.output.background;
    // One sample per renderFrame call; the accumulation buffer does the
    // averaging, which lets the preview reuse this class progressively.
    renderer_.setParam("pixelSamples", 1);
    renderer_.setParam("aoSamples", 2);
    renderer_.setParam("backgroundColor", Vec4{bg.x, bg.y, bg.z, 1.0f});
    renderer_.commit();

    camera_.setParam("aspect", static_cast<float>(width) / static_cast<float>(height));
    camera_.commit();

    framebuffer_.commit();
}

const std::vector<uint32_t>& FrameRenderer::render(const Camera& camera)
{
    camera_.setParam("position", camera.position);
    camera_.setParam("direction", normalize(camera.target - camera.position));
    camera_.setParam("up", camera.up);
    camera_.setParam("fovy", camera.fov_y_degrees);
    camera_.commit();

    framebuffer_.resetAccumulation();
    for (int sample = 0; sample < samples_per_pixel_; ++sample)
        framebuffer_.renderFrame(renderer_, camera_, world_);

    const auto* mapped = static_cast<const uint32_t*>(framebuffer_.map(OSP_FB_COLOR));
    if (mapped == nullptr)
        throw std::runtime_error("failed to map the OSPRay colour buffer");
    pixels_.assign(mapped, mapped + pixels_.size());
    framebuffer_.unmap(const_cast<uint32_t*>(mapped));
    return pixels_;
}

} // namespace ospr
