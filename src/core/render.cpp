#include "ospr/render.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include "ospr/math.h"

namespace ospr {
namespace {

// Without this OSPRay swallows "found unknown parameter" warnings, which is how
// a renamed parameter turns into a silently wrong picture rather than an error.
void status_callback(void*, const char* message)
{
    std::cerr << "ospray: " << message;
}

void error_callback(void*, OSPError code, const char* message)
{
    std::cerr << "ospray error " << static_cast<int>(code) << ": " << message << "\n";
}

} // namespace

Device::Device(int& argc, const char** argv)
{
    const OSPError error = ospInit(&argc, argv);
    if (error != OSP_NO_ERROR)
        throw std::runtime_error(
            "ospInit failed with code " + std::to_string(static_cast<int>(error)));

    // The denoiser lives in its own module; without this ospNewImageOperation
    // returns a null handle and the frame is simply never denoised.
    if (ospLoadModule("denoiser") != OSP_NO_ERROR)
        std::cerr << "ospray: denoiser module unavailable; rendering without it\n";

    OSPDevice device = ospGetCurrentDevice();
    ospDeviceSetStatusCallback(device, status_callback, nullptr);
    ospDeviceSetErrorCallback(device, error_callback, nullptr);
    ospDeviceSetParam(device, "warnAsError", OSP_BOOL, &warn_as_error_);
    ospDeviceSetParam(device, "logLevel", OSP_INT, &log_level_);
    ospDeviceCommit(device);
    ospDeviceRelease(device);
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
    , scene_(script.session)
    , renderer_(script.session.renderer.type)
    , camera_("perspective")
    , pixels_(static_cast<std::size_t>(width) * height)
{
    const Vec3& background = script.session.renderer.background;
    // One sample per renderFrame call; the accumulation buffer does the
    // averaging, which lets the preview reuse this class progressively.
    renderer_.setParam("pixelSamples", 1);
    renderer_.setParam("backgroundColor", Vec4{background.x, background.y, background.z, 1.0f});
    if (script.session.renderer.type == "scivis")
        renderer_.setParam("aoSamples", 2);
    else
        renderer_.setParam("maxPathLength", 8);
    renderer_.commit();

    camera_.setParam("aspect", static_cast<float>(width) / static_cast<float>(height));
    camera_.commit();

    // The denoiser wants albedo and normal to tell texture from noise; without
    // them a low-sample preview loses its surface detail along with the grain.
    int channels = OSP_FB_COLOR | OSP_FB_ACCUM;
    if (script.session.renderer.denoise)
        channels |= OSP_FB_ALBEDO | OSP_FB_NORMAL;
    framebuffer_ = ospray::cpp::FrameBuffer(width, height, OSP_FB_SRGBA, channels);

    if (script.session.renderer.denoise) {
        ospray::cpp::ImageOperation denoiser("denoiser");
        if (denoiser.handle() != nullptr) {
            denoiser.commit();
            framebuffer_.setParam("imageOperation", ospray::cpp::CopiedData(denoiser));
        }
    }
    framebuffer_.commit();
}

const std::vector<uint32_t>& FrameRenderer::render(
    const Camera& camera, const OpacityCurve& opacity)
{
    scene_.apply_opacity(opacity);

    camera_.setParam("position", camera.position);
    camera_.setParam("direction", normalize(camera.target - camera.position));
    camera_.setParam("up", camera.up);
    camera_.setParam("fovy", camera.fov_y_degrees);
    camera_.commit();

    framebuffer_.resetAccumulation();
    for (int sample = 0; sample < samples_per_pixel_; ++sample)
        framebuffer_.renderFrame(renderer_, camera_, scene_.world());

    const auto* mapped = static_cast<const uint32_t*>(framebuffer_.map(OSP_FB_COLOR));
    if (mapped == nullptr)
        throw std::runtime_error("failed to map the OSPRay colour buffer");
    pixels_.assign(mapped, mapped + pixels_.size());
    framebuffer_.unmap(const_cast<uint32_t*>(mapped));
    return pixels_;
}

} // namespace ospr
