#pragma once

#include <cstdint>
#include <vector>

#include <ospray/ospray_cpp.h>

#include "ospr/camera.h"
#include "ospr/opacity_curve.h"
#include "ospr/scene.h"
#include "ospr/script.h"

namespace ospr {

// Initialises the OSPRay device; must outlive every FrameRenderer. Throws
// std::runtime_error if the device cannot be created.
class Device
{
public:
    Device(int& argc, const char** argv);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

private:
    bool warn_as_error_{false};
    int log_level_{OSP_LOG_WARNING};
};

// Holds the world, renderer and framebuffer for one output resolution. render()
// accumulates samples_per_pixel samples and returns the sRGB frame.
class FrameRenderer
{
public:
    FrameRenderer(const Script& script, int width, int height, int samples_per_pixel);

    // Batch path: reset, accumulate every sample, return the frame.
    const std::vector<uint32_t>& render(const Camera& camera, const OpacityCurve& opacity);

    // Progressive path for the preview. set_* discard the accumulation buffer
    // only when the value actually changed, so an idle UI keeps converging.
    void set_camera(const Camera& camera);
    void set_opacity(const OpacityCurve& opacity);
    void reset();
    bool accumulate(int samples = 1);
    const std::vector<uint32_t>& pixels();

    int accumulated() const { return accumulated_; }
    int target_samples() const { return samples_per_pixel_; }

    Scene& scene() { return scene_; }
    const Bounds& bounds() const { return scene_.bounds(); }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    int width_;
    int height_;
    int samples_per_pixel_;
    Scene scene_;
    ospray::cpp::Renderer renderer_;
    ospray::cpp::Camera camera_;
    ospray::cpp::FrameBuffer framebuffer_;
    std::vector<uint32_t> pixels_;
    Camera current_camera_;
    bool camera_valid_{false};
    int accumulated_{0};
};

} // namespace ospr
