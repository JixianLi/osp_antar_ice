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

constexpr int PREVIEW_WIDTH = 256;
constexpr int PREVIEW_HEIGHT = 144;
constexpr float DEGREES_TO_RADIANS = 3.14159265358979323846f / 180.0f;

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

// The framebuffer's first row is the bottom of the image, so the quad is drawn
// with its v coordinate flipped rather than copying the rows around.
void draw_render(GLuint texture, int window_width, int window_height)
{
    const float target = static_cast<float>(PREVIEW_WIDTH) / PREVIEW_HEIGHT;
    const float actual = static_cast<float>(window_width) / window_height;
    float width = static_cast<float>(window_width);
    float height = static_cast<float>(window_height);
    if (actual > target)
        width = height * target;
    else
        height = width / target;

    const ImVec2 origin((window_width - width) * 0.5f, (window_height - height) * 0.5f);
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

        std::cout << "loading scene...\n" << std::flush;
        ospr::FrameRenderer renderer(
            script, PREVIEW_WIDTH, PREVIEW_HEIGHT, script.session.renderer.samples_per_pixel);
        const ospr::Bounds& bounds = renderer.bounds();
        ospr::frame_scene(script.orbit, bounds,
            static_cast<float>(PREVIEW_WIDTH) / PREVIEW_HEIGHT);
        std::cout << "ready\n" << std::flush;

        OrbitState orbit;
        orbit.azimuth_degrees = script.orbit.azimuth_start_degrees;
        orbit.elevation_degrees = script.orbit.elevation_degrees;
        orbit.fov_y_degrees = script.orbit.fov_y_degrees;
        orbit.up = script.orbit.up;
        orbit.center = script.orbit.center;
        orbit.radius = script.orbit.radius;

        std::vector<ospr::LightSpec> lights = script.session.lights;
        const GLuint texture = make_texture();

        float time_seconds = 0.0f;
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
                PREVIEW_WIDTH,
                PREVIEW_HEIGHT,
                renderer.accumulated(),
                renderer.target_samples());
            ImGui::Separator();

            if (ImGui::CollapsingHeader("timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::SliderFloat(
                        "t (s)", &time_seconds, 0.0f, script.timeline.duration_seconds))
                    dirty = true;
                ImGui::Checkbox("camera follows script", &follow_script_camera);
            }

            if (ImGui::CollapsingHeader("camera", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Button("frame scene")) {
                    ospr::OrbitSpec fitted;
                    fitted.fov_y_degrees = orbit.fov_y_degrees;
                    ospr::frame_scene(fitted, bounds,
                        static_cast<float>(PREVIEW_WIDTH) / PREVIEW_HEIGHT);
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
                for (std::size_t index = 0; index < renderer.scene().volume_count();
                     ++index) {
                    ImGui::PushID(static_cast<int>(1000 + index));
                    float density = renderer.scene().volume_spec(index).density_scale;
                    // Extinction per world unit. The scene is ~1e6 across, so the
                    // useful range is tiny and a plausible-looking 1.0 is opaque.
                    if (ImGui::SliderFloat("density", &density, 1e-6f, 2e-4f, "%.2e",
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

            const ospr::Camera camera = follow_script_camera
                ? ospr::camera_for(script, time_seconds)
                : orbit.camera();
            renderer.set_camera(camera);
            if (dirty) {
                renderer.set_opacity(ospr::opacity_at(script.keyframes, time_seconds));
                dirty = false;
            }

            renderer.accumulate(1);
            const std::vector<uint32_t>& pixels = renderer.pixels();
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                GL_RGBA,
                PREVIEW_WIDTH,
                PREVIEW_HEIGHT,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                pixels.data());

            int window_width = 0;
            int window_height = 0;
            glfwGetFramebufferSize(window, &window_width, &window_height);
            draw_render(texture, window_width, window_height);

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
