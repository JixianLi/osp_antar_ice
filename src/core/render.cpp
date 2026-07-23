#include "ospr/render.h"

#include <algorithm>
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
    renderer_type_ = script.session.renderer.type;
    // One sample per renderFrame call; the accumulation buffer does the
    // averaging, which lets the preview reuse this class progressively.
    renderer_.setParam("pixelSamples", 1);
    if (renderer_type_ == "scivis")
        renderer_.setParam("aoSamples", ao_samples_);
    else {
        renderer_.setParam("maxPathLength", 8);
        renderer_.setParam("lightSamples", light_samples_);
    }
    set_background(
        script.session.renderer.background_top, script.session.renderer.background_bottom);

    denoise_ = script.session.renderer.denoise;
    camera_.setParam("aspect", static_cast<float>(width) / static_cast<float>(height));
    camera_.commit();
    rebuild_framebuffer();
}

void FrameRenderer::rebuild_framebuffer()
{
    // The denoiser wants albedo and normal to tell texture from noise; without
    // them a low-sample preview loses its surface detail along with the grain.
    int channels = OSP_FB_COLOR | OSP_FB_ACCUM;
    if (denoise_)
        channels |= OSP_FB_ALBEDO | OSP_FB_NORMAL;
    framebuffer_ = ospray::cpp::FrameBuffer(width_, height_, OSP_FB_SRGBA, channels);

    if (denoise_) {
        ospray::cpp::ImageOperation denoiser("denoiser");
        if (denoiser.handle() != nullptr) {
            denoiser.commit();
            framebuffer_.setParam("imageOperation", ospray::cpp::CopiedData(denoiser));
        }
    }
    framebuffer_.commit();
    accumulated_ = 0;
}

void FrameRenderer::set_resolution(int width, int height)
{
    if (width == width_ && height == height_)
        return;
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * height, 0);
    camera_.setParam("aspect", static_cast<float>(width) / static_cast<float>(height));
    camera_.commit();
    rebuild_framebuffer();
}

namespace {

bool same(Vec3 a, Vec3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

void FrameRenderer::set_background(Vec3 top, Vec3 bottom)
{
    // A tall, 1-wide backplate: OSPRay samples it in normalised screen coords,
    // so a single column is a pure vertical gradient. Row 0 is the bottom of the
    // frame. HSV keeps a coloured gradient from desaturating at the midpoint.
    constexpr int STEPS = 256;
    std::vector<Vec3> column(STEPS);
    for (int index = 0; index < STEPS; ++index) {
        const float t = static_cast<float>(index) / (STEPS - 1);
        column[index] = lerp_hsv(bottom, top, t);
    }

    ospray::cpp::Texture backplate("texture2d");
    backplate.setParam("format", OSP_TEXTURE_RGB32F);
    backplate.setParam("filter", OSP_TEXTURE_FILTER_LINEAR);
    backplate.setParam("data",
        ospray::cpp::CopiedData(column.data(), Vec2ul{1, static_cast<unsigned long long>(STEPS)}));
    backplate.commit();

    renderer_.setParam("map_backplate", backplate);
    // Fallback for anything the backplate does not cover (e.g. regions outside
    // the texture under some wrap modes).
    renderer_.setParam("backgroundColor", Vec4{bottom.x, bottom.y, bottom.z, 1.0f});
    renderer_.commit();
    reset();
}

void FrameRenderer::set_target_samples(int samples)
{
    samples_per_pixel_ = std::max(1, samples);
    reset();
}

void FrameRenderer::set_ao_samples(int samples)
{
    ao_samples_ = std::max(0, samples);
    if (renderer_type_ != "scivis")
        return;
    renderer_.setParam("aoSamples", ao_samples_);
    renderer_.commit();
    reset();
}

void FrameRenderer::set_light_samples(int samples)
{
    light_samples_ = std::max(1, samples);
    if (renderer_type_ != "pathtracer")
        return;
    renderer_.setParam("lightSamples", light_samples_);
    renderer_.commit();
    reset();
}

void FrameRenderer::set_camera(const Camera& camera)
{
    if (camera_valid_ && same(camera.position, current_camera_.position)
        && same(camera.target, current_camera_.target)
        && same(camera.up, current_camera_.up)
        && camera.fov_y_degrees == current_camera_.fov_y_degrees)
        return;

    current_camera_ = camera;
    camera_valid_ = true;
    camera_.setParam("position", camera.position);
    camera_.setParam("direction", normalize(camera.target - camera.position));
    camera_.setParam("up", camera.up);
    camera_.setParam("fovy", camera.fov_y_degrees);
    camera_.commit();
    reset();
}

void FrameRenderer::set_opacity(const OpacityCurve& opacity)
{
    scene_.apply_opacity(opacity);
    reset();
}

void FrameRenderer::reset()
{
    // set_background runs from the constructor before the framebuffer exists.
    if (framebuffer_.handle() != nullptr)
        framebuffer_.resetAccumulation();
    accumulated_ = 0;
}

bool FrameRenderer::accumulate(int samples)
{
    for (int sample = 0; sample < samples && accumulated_ < samples_per_pixel_; ++sample) {
        framebuffer_.renderFrame(renderer_, camera_, scene_.world());
        ++accumulated_;
    }
    return accumulated_ < samples_per_pixel_;
}

const std::vector<uint32_t>& FrameRenderer::pixels()
{
    const auto* mapped = static_cast<const uint32_t*>(framebuffer_.map(OSP_FB_COLOR));
    if (mapped == nullptr)
        throw std::runtime_error("failed to map the OSPRay colour buffer");
    pixels_.assign(mapped, mapped + pixels_.size());
    framebuffer_.unmap(const_cast<uint32_t*>(mapped));
    return pixels_;
}

const std::vector<uint32_t>& FrameRenderer::render(
    const Camera& camera, const OpacityCurve& opacity)
{
    scene_.apply_opacity(opacity);
    set_camera(camera);
    reset();
    accumulate(samples_per_pixel_);
    return pixels();
}

} // namespace ospr
