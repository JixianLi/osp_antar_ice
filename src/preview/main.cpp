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

// Draggable opacity-vs-layer_id editor. x is layer_id in [0, 5], y is opacity in
// [0, 1]. Left-drag a point to move it, double-click empty space to add one,
// right-click a point to delete it. Returns true when the curve changed.
bool opacity_editor(const char* id, ospr::OpacityCurve& curve)
{
    constexpr float LAYER_MAX = 5.0f;
    const ImVec2 size(320.0f, 150.0f);
    ImGui::PushID(id);
    ImGui::InvisibleButton("canvas", size);
    const ImVec2 p0 = ImGui::GetItemRectMin();
    const ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p0, p1, IM_COL32(18, 18, 24, 255));
    draw->AddRect(p0, p1, IM_COL32(70, 70, 85, 255));

    const auto to_x = [&](float layer) { return p0.x + (layer / LAYER_MAX) * (p1.x - p0.x); };
    const auto to_y = [&](float opacity) { return p1.y - opacity * (p1.y - p0.y); };
    const auto from = [&](ImVec2 pixel) {
        return ospr::OpacityPoint{
            std::clamp((pixel.x - p0.x) / (p1.x - p0.x) * LAYER_MAX, 0.0f, LAYER_MAX),
            std::clamp((p1.y - pixel.y) / (p1.y - p0.y), 0.0f, 1.0f)};
    };

    for (int layer = 1; layer < 5; ++layer)
        draw->AddLine(ImVec2(to_x(static_cast<float>(layer)), p0.y),
            ImVec2(to_x(static_cast<float>(layer)), p1.y),
            IM_COL32(45, 45, 55, 255));

    ImVec2 previous;
    for (int step = 0; step <= 64; ++step) {
        const float layer = LAYER_MAX * step / 64.0f;
        const ImVec2 point(to_x(layer), to_y(curve.at(layer)));
        if (step > 0)
            draw->AddLine(previous, point, IM_COL32(120, 180, 255, 255), 2.0f);
        previous = point;
    }

    bool changed = false;
    static int drag = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    int hovered = -1;
    for (std::size_t index = 0; index < curve.points.size(); ++index) {
        const ImVec2 centre(to_x(curve.points[index].layer), to_y(curve.points[index].opacity));
        const float dx = mouse.x - centre.x;
        const float dy = mouse.y - centre.y;
        const bool near = dx * dx + dy * dy < 64.0f;
        if (near)
            hovered = static_cast<int>(index);
        draw->AddCircleFilled(centre, near ? 6.0f : 4.0f,
            near ? IM_COL32(255, 230, 140, 255) : IM_COL32(255, 200, 80, 255));
    }

    if (ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        drag = hovered;
    if (drag >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)
        && drag < static_cast<int>(curve.points.size())) {
        curve.points[drag] = from(mouse);
        changed = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        drag = -1;

    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)
        && curve.points.size() > 2) {
        curve.points.erase(curve.points.begin() + hovered);
        changed = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        curve.points.push_back(from(mouse));
        changed = true;
    }

    if (changed) {
        std::sort(curve.points.begin(),
            curve.points.end(),
            [](const ospr::OpacityPoint& a, const ospr::OpacityPoint& b) {
                return a.layer < b.layer;
            });
    }
    ImGui::PopID();
    return changed;
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

        const int keyframe_count = static_cast<int>(script.keyframes.size());
        const int last_frame = std::max(0, ospr::frame_count(script) - 1);
        int keyframe_index = 0;
        bool playing = false;
        int play_frame = 0;
        float play_accumulator = 0.0f;
        bool follow_script_camera = true;
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

            // Play walks every interpolated frame at 5 fps; scrubbing lands only
            // on keyframes. Stopping snaps to the next keyframe either way.
            if (playing) {
                play_accumulator += io.DeltaTime;
                while (play_accumulator >= 0.2f) {
                    play_accumulator -= 0.2f;
                    ++play_frame;
                    dirty = true;
                    if (play_frame >= last_frame) {
                        play_frame = last_frame;
                        playing = false;
                        keyframe_index = keyframe_count - 1;
                        break;
                    }
                }
            }
            const float u = playing ? ospr::frame_to_param(script, play_frame)
                                    : static_cast<float>(keyframe_index);

            if (ImGui::CollapsingHeader("timeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (playing) {
                    if (ImGui::Button("stop")) {
                        playing = false;
                        keyframe_index = std::clamp(
                            static_cast<int>(std::ceil(ospr::frame_to_param(script, play_frame))),
                            0,
                            keyframe_count - 1);
                        dirty = true;
                    }
                    ImGui::SameLine();
                    ImGui::Text("playing  frame %d / %d  u=%.2f", play_frame, last_frame, u);
                } else {
                    if (ImGui::Button("play")) {
                        playing = true;
                        play_frame = ospr::keyframe_frame(script, keyframe_index);
                        play_accumulator = 0.0f;
                    }
                    ImGui::SameLine();
                    if (ImGui::SliderInt(
                            "keyframe", &keyframe_index, 0, keyframe_count - 1))
                        dirty = true;
                }
                ImGui::Checkbox("camera follows script", &follow_script_camera);
            }

            if (!playing
                && ImGui::CollapsingHeader("transfer function", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("opacity at keyframe %d", keyframe_index);
                if (opacity_editor("opacity", script.keyframes[keyframe_index].opacity))
                    dirty = true;
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

            const ospr::Camera camera = (playing || follow_script_camera)
                ? ospr::camera_for(script, u)
                : orbit.camera();
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
