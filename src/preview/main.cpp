// Interactive scene designer. The render is deliberately tiny -- the point is
// to judge colour, light and peel timing, then hand the same session file to
// ospr_render on Lonestar6 at 4K. Preview and final share an aspect ratio so
// framing transfers exactly.

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#define GL_SILENCE_DEPRECATION
// GLFW pulls in the legacy gl.h otherwise, which collides with gl3.h.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <OpenGL/gl3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "ospr/keyframe.h"
#include "ospr/render.h"
#include "ospr/scene.h"
#include "ospr/script.h"

namespace {

// Preview is always 16:9 to match the 4K output; the selector picks the long
// side. 1024 x 576 is a real preview at speed, not a thumbnail.
constexpr float VIEW_ASPECT = 16.0f / 9.0f;
constexpr int RESOLUTION_LONG_SIDES[] = {128, 256, 512, 1024};
constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;

int height_for(int long_side)
{
    return static_cast<int>(std::lround(long_side / VIEW_ASPECT));
}

// Free orbit the mouse drives, seeded from the script's orbit so the preview
// opens on the shot rather than somewhere arbitrary.
struct OrbitState
{
    ospr::Vec3 center;
    float radius{1.0f};
    float azimuth_degrees{0.0f};
    float elevation_degrees{30.0f};
    float fov_y_degrees{40.0f};
    ospr::Vec3 up{0.0f, 0.0f, 1.0f};

    ospr::Camera camera() const
    {
        const float azimuth = azimuth_degrees * DEGREES_TO_RADIANS;
        const float elevation = elevation_degrees * DEGREES_TO_RADIANS;
        ospr::Camera camera;
        camera.target = center;
        camera.position = {center.x + radius * std::cos(elevation) * std::cos(azimuth),
            center.y + radius * std::cos(elevation) * std::sin(azimuth),
            center.z + radius * std::sin(elevation)};
        camera.up = up;
        camera.fov_y_degrees = fov_y_degrees;
        return camera;
    }
};

GLuint make_texture()
{
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

// Placed in ImGui's logical coordinate space (points), NOT framebuffer pixels:
// on a Retina display the framebuffer is 2x the window, and mixing the two puts
// the image at 2x offset so only a quarter shows. The framebuffer's first row is
// the bottom of the image, so the quad is drawn with its v coordinate flipped.
void draw_render(GLuint texture, float view_width, float view_height)
{
    const float target = VIEW_ASPECT;
    const float actual = view_width / view_height;
    float width = view_width;
    float height = view_height;
    if (actual > target)
        width = height * target;
    else
        height = width / target;

    const ImVec2 origin((view_width - width) * 0.5f, (view_height - height) * 0.5f);
    ImGui::GetBackgroundDrawList()->AddImage(
        static_cast<ImTextureID>(static_cast<intptr_t>(texture)),
        origin,
        ImVec2(origin.x + width, origin.y + height),
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 2) {
            std::cerr << "usage: ospr_preview <session.json>\n";
            return 2;
        }
        const std::string script_path = argv[1];
        ospr::Script script = ospr::load_script(script_path);

        if (!glfwInit())
            throw std::runtime_error("glfwInit failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

        GLFWwindow* window = glfwCreateWindow(1280, 800, "ospr_preview", nullptr, nullptr);
        if (window == nullptr)
            throw std::runtime_error("failed to create a window");
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 150");

        int ospray_argc = argc;
        const ospr::Device device(ospray_argc, const_cast<const char**>(argv));

        int resolution_index = 1; // 256
        int preview_width = RESOLUTION_LONG_SIDES[resolution_index];
        int preview_height = height_for(preview_width);

        std::cout << "loading scene...\n" << std::flush;
        ospr::FrameRenderer renderer(
            script, preview_width, preview_height, script.session.renderer.samples_per_pixel);
        const ospr::Bounds& bounds = renderer.bounds();
        ospr::OrbitSpec fit;
        fit.fov_y_degrees = script.keyframes.empty() ? 40.0f : script.keyframes[0].fov_y_degrees;
        fit.up = script.up;
        ospr::frame_scene(fit, bounds, VIEW_ASPECT);
        std::cout << "ready\n" << std::flush;

        // The free camera opens on the first keyframe pose so the preview starts
        // where the shot does.
        OrbitState orbit;
        orbit.up = script.up;
        orbit.center = fit.center;
        if (!script.keyframes.empty()) {
            orbit.azimuth_degrees = script.keyframes[0].azimuth_degrees;
            orbit.elevation_degrees = script.keyframes[0].elevation_degrees;
            orbit.fov_y_degrees = script.keyframes[0].fov_y_degrees;
            orbit.radius = script.keyframes[0].radius;
        } else {
            orbit.elevation_degrees = fit.elevation_degrees;
            orbit.fov_y_degrees = fit.fov_y_degrees;
            orbit.radius = fit.radius;
        }

        std::vector<ospr::LightSpec> lights = script.session.lights;
        ospr::Vec3 background_top = script.session.renderer.background_top;
        ospr::Vec3 background_bottom = script.session.renderer.background_bottom;
        const GLuint texture = make_texture();

        const int last_frame = std::max(0, ospr::frame_count(script) - 1);
        int frame_index = 0;
        bool follow_script_camera = false;
        bool dirty = true;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantCaptureMouse) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                    orbit.azimuth_degrees -= drag.x * 0.3f;
                    orbit.elevation_degrees
                        = std::clamp(orbit.elevation_degrees + drag.y * 0.3f, -89.0f, 89.0f);
                    follow_script_camera = false;
                }
                if (io.MouseWheel != 0.0f) {
                    orbit.radius *= std::pow(0.9f, io.MouseWheel);
                    follow_script_camera = false;
                }
            }

            ImGui::SetNextWindowSize(ImVec2(370, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("session");

            ImGui::Text("%d x %d   %d / %d spp",
                preview_width,
                preview_height,
                renderer.accumulated(),
                renderer.target_samples());
            static const char* const RESOLUTION_LABELS[] = {"128", "256", "512", "1024"};
            if (ImGui::Combo("resolution", &resolution_index, RESOLUTION_LABELS, 4)) {
                preview_width = RESOLUTION_LONG_SIDES[resolution_index];
                preview_height = height_for(preview_width);
                renderer.set_resolution(preview_width, preview_height);
            }
            ImGui::Separator();

            if (ImGui::CollapsingHeader("timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::SliderInt("frame", &frame_index, 0, last_frame))
                    dirty = true;
                ImGui::SameLine();
                ImGui::Text("u=%.2f", ospr::frame_to_param(script, frame_index));
                ImGui::Checkbox("camera follows script", &follow_script_camera);
            }

            if (ImGui::CollapsingHeader("camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("frame scene")) {
                    ospr::OrbitSpec fitted;
                    fitted.fov_y_degrees = orbit.fov_y_degrees;
                    ospr::frame_scene(fitted, bounds, VIEW_ASPECT);
                    orbit.center = fitted.center;
                    orbit.radius = fitted.radius;
                    follow_script_camera = false;
                }
                ImGui::SameLine();
                ImGui::Text("centre %.0f %.0f %.0f",
                    orbit.center.x, orbit.center.y, orbit.center.z);
                ImGui::SliderFloat("azimuth", &orbit.azimuth_degrees, -360.0f, 360.0f);
                ImGui::SliderFloat("elevation", &orbit.elevation_degrees, -89.0f, 89.0f);
                ImGui::SliderFloat("fov", &orbit.fov_y_degrees, 10.0f, 90.0f);
                ImGui::SliderFloat("radius", &orbit.radius, bounds.diagonal() * 0.15f,
                    bounds.diagonal() * 3.0f);
            }

            if (ImGui::CollapsingHeader("background", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto to255 = [](const ospr::Vec3& c, int out[3]) {
                    out[0] = static_cast<int>(std::lround(c.x * 255.0f));
                    out[1] = static_cast<int>(std::lround(c.y * 255.0f));
                    out[2] = static_cast<int>(std::lround(c.z * 255.0f));
                };
                int top[3];
                int bottom[3];
                to255(background_top, top);
                to255(background_bottom, bottom);
                bool changed = false;
                changed |= ImGui::SliderInt3("top", top, 0, 255);
                changed |= ImGui::SliderInt3("bottom", bottom, 0, 255);
                if (changed) {
                    background_top = {top[0] / 255.0f, top[1] / 255.0f, top[2] / 255.0f};
                    background_bottom
                        = {bottom[0] / 255.0f, bottom[1] / 255.0f, bottom[2] / 255.0f};
                    renderer.set_background(background_top, background_bottom);
                }
            }

            if (ImGui::CollapsingHeader("lights", ImGuiTreeNodeFlags_DefaultOpen)) {
                bool changed = false;
                for (std::size_t index = 0; index < lights.size(); ++index) {
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TextUnformatted(lights[index].type.c_str());
                    changed |= ImGui::SliderFloat(
                        "intensity", &lights[index].intensity, 0.0f, 4.0f);
                    changed |= ImGui::ColorEdit3("colour", &lights[index].color.x);
                    if (lights[index].type == "distant")
                        changed |= ImGui::SliderFloat3(
                            "direction", &lights[index].direction.x, -1.0f, 1.0f);
                    ImGui::PopID();
                    ImGui::Separator();
                }
                if (changed) {
                    renderer.scene().set_lights(lights);
                    renderer.reset();
                }
            }

            if (ImGui::CollapsingHeader("volume", ImGuiTreeNodeFlags_DefaultOpen)) {
                float z_scale = renderer.scene().z_scale();
                if (ImGui::SliderFloat("z scale", &z_scale, 25.0f, 50.0f, "%.1f")) {
                    renderer.scene().set_z_scale(z_scale);
                    renderer.reset();
                }
                for (std::size_t index = 0; index < renderer.scene().volume_count();
                     ++index) {
                    ImGui::PushID(static_cast<int>(1000 + index));
                    float density = renderer.scene().volume_spec(index).density_scale;
                    // Extinction per normalised unit; the scene spans [-1, 1],
                    // so this is an O(1) knob.
                    if (ImGui::SliderFloat("density", &density, 0.1f, 100.0f, "%.2f",
                            ImGuiSliderFlags_Logarithmic)) {
                        renderer.scene().set_density_scale(index, density);
                        renderer.reset();
                    }
                    ImGui::PopID();
                }
            }

            if (ImGui::CollapsingHeader("surfaces", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::size_t index = 0; index < renderer.scene().surface_count();
                     ++index) {
                    ImGui::PushID(static_cast<int>(2000 + index));
                    const ospr::SurfaceSpec& spec = renderer.scene().surface_spec(index);
                    ImGui::TextUnformatted(spec.path.c_str());
                    float range[2]{spec.value_range.lo, spec.value_range.hi};
                    if (ImGui::DragFloat2("depth range", range, 10.0f, 0.0f, 5000.0f)) {
                        renderer.scene().set_surface_range(index, {range[0], range[1]});
                        renderer.reset();
                    }
                    ImGui::PopID();
                    ImGui::Separator();
                }
            }

            ImGui::End();

            const float u = ospr::frame_to_param(script, frame_index);
            const ospr::Camera camera
                = follow_script_camera ? ospr::camera_for(script, u) : orbit.camera();
            renderer.set_camera(camera);
            if (dirty) {
                renderer.set_opacity(ospr::opacity_at(script.keyframes, u));
                dirty = false;
            }

            renderer.accumulate(1);
            const std::vector<uint32_t>& pixels = renderer.pixels();
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGBA,
                preview_width,
                preview_height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());

            draw_render(texture, io.DisplaySize.x, io.DisplaySize.y);

            int window_width = 0;
            int window_height = 0;
            glfwGetFramebufferSize(window, &window_width, &window_height);
            ImGui::Render();
            glViewport(0, 0, window_width, window_height);
            glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ospr_preview: " << error.what() << "\n";
        return 1;
    }
}
