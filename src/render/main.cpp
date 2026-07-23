#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "ospr/colormap.h"
#include "ospr/image.h"
#include "ospr/keyframe.h"
#include "ospr/render.h"
#include "ospr/scene.h"
#include "ospr/script.h"
#include "ospr/vtk_xml.h"

namespace {

struct Options
{
    std::string script_path;
    std::string output_directory;
    std::string info_path;
    std::string swatch_colormap;
    std::string swatch_png;
    ospr::ColorMapTrim trim;
    int single_frame{-1};
};

void print_usage()
{
    std::cerr << "usage: ospr_render <script.json> [--out DIR] [--frame N]\n"
                 "       ospr_render --info <file.vti>\n"
                 "       ospr_render --swatch <colormap.xml> <out.png>\n"
                 "  --out DIR      override the script's output.dir\n"
                 "  --frame N      render only frame N\n"
                 "  --info FILE    print a volume's bounds and arrays, then exit\n"
                 "  --swatch A B   write colormap A as a PNG strip to B, then exit\n"
                 "  --trim LO HI   restrict a colormap to a sub-range of itself\n";
}

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--out" && index + 1 < argc) {
            options.output_directory = argv[++index];
        } else if (arg == "--frame" && index + 1 < argc) {
            options.single_frame = std::stoi(argv[++index]);
        } else if (arg == "--info" && index + 1 < argc) {
            options.info_path = argv[++index];
        } else if (arg == "--trim" && index + 2 < argc) {
            options.trim.lo = std::stof(argv[++index]);
            options.trim.hi = std::stof(argv[++index]);
        } else if (arg == "--swatch" && index + 2 < argc) {
            options.swatch_colormap = argv[++index];
            options.swatch_png = argv[++index];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        } else if (options.script_path.empty()) {
            options.script_path = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }
    if (options.script_path.empty() && options.info_path.empty()
        && options.swatch_colormap.empty())
        throw std::runtime_error("no script given");
    return options;
}

// Authoring keyframes for a 1050 x 770 km domain by hand needs the bounds in
// front of you, and the finite range matters because the resampler leaves both
// NaN and out-of-domain fill in the scalar arrays.
void print_arrays(const std::vector<ospr::DataArray>& arrays)
{
    std::cout << "  arrays\n";
    for (const ospr::DataArray& array : arrays) {
        float lo = std::numeric_limits<float>::infinity();
        float hi = -std::numeric_limits<float>::infinity();
        double sum = 0.0;
        std::size_t nan_count = 0;
        for (const float value : array.values) {
            if (std::isnan(value)) {
                ++nan_count;
                continue;
            }
            lo = std::min(lo, value);
            hi = std::max(hi, value);
            sum += value;
        }
        const std::size_t finite = array.values.size() - nan_count;
        std::cout << "    " << array.name << "  finite range " << lo << " .. " << hi
                  << "   mean " << (finite ? sum / finite : 0.0) << "   nan " << nan_count
                  << " (" << (100.0 * nan_count / array.values.size()) << "%)\n";
    }
}

void print_surface_info(const std::string& path)
{
    const ospr::StructuredGrid grid = ospr::read_vts(path);
    std::cout << path << "\n"
              << "  dims     " << grid.dims[0] << " x " << grid.dims[1] << " x "
              << grid.dims[2] << "  (" << grid.point_count() << " points)\n";

    ospr::Vec3 lo{std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity()};
    ospr::Vec3 hi{-lo.x, -lo.y, -lo.z};
    for (const ospr::Vec3& point : grid.points) {
        lo = {std::min(lo.x, point.x), std::min(lo.y, point.y), std::min(lo.z, point.z)};
        hi = {std::max(hi.x, point.x), std::max(hi.y, point.y), std::max(hi.z, point.z)};
    }
    std::cout << "  bounds   " << lo.x << " .. " << hi.x << "   " << lo.y << " .. " << hi.y
              << "   " << lo.z << " .. " << hi.z << "\n";
    print_arrays(grid.point_arrays);
}

void print_volume_info(const std::string& path)
{
    const ospr::ImageData data = ospr::read_vti(path);
    std::cout << path << "\n"
              << "  dims     " << data.dims[0] << " x " << data.dims[1] << " x "
              << data.dims[2] << "  (" << data.point_count() << " points)\n"
              << "  origin   " << data.origin[0] << ", " << data.origin[1] << ", "
              << data.origin[2] << "\n"
              << "  spacing  " << data.spacing[0] << ", " << data.spacing[1] << ", "
              << data.spacing[2] << "\n";

    std::cout << "  bounds";
    for (int axis = 0; axis < 3; ++axis) {
        const double lo = data.origin[axis];
        const double hi = lo + data.spacing[axis] * (data.dims[axis] - 1);
        std::cout << "   " << lo << " .. " << hi;
    }
    std::cout << "\n";
    print_arrays(data.point_arrays);
}

// Checking a colormap by eye before committing it to a long render, and the
// only way to diff our Lab interpolation against ParaView's.
void write_colormap_swatch(
    const std::string& colormap_path, const std::string& png_path, ospr::ColorMapTrim trim)
{
    constexpr int WIDTH = 512;
    constexpr int HEIGHT = 64;
    const ospr::ColorMap colormap = ospr::load_colormap(colormap_path, "", WIDTH, trim);

    std::vector<uint32_t> pixels(static_cast<std::size_t>(WIDTH) * HEIGHT);
    for (int column = 0; column < WIDTH; ++column) {
        const ospr::Vec3 color = colormap.colors[column];
        const auto channel = [](float value) {
            return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };
        const uint32_t packed = channel(color.x) | (channel(color.y) << 8)
            | (channel(color.z) << 16) | (0xFFu << 24);
        for (int row = 0; row < HEIGHT; ++row)
            pixels[static_cast<std::size_t>(row) * WIDTH + column] = packed;
    }

    ospr::write_png_rgba(png_path, WIDTH, HEIGHT, pixels.data());
    std::cout << colormap_path << "  -> " << png_path << "  (\"" << colormap.name << "\")\n";
}

std::string frame_path(const std::filesystem::path& directory, int frame_index)
{
    char name[32];
    std::snprintf(name, sizeof(name), "frame_%05d.png", frame_index);
    return (directory / name).string();
}

} // namespace

int main(int argc, char** argv)
{
    // Progress must be visible when stdout is a file, not just a terminal;
    // a long render otherwise looks identical to a hung one.
    std::cout << std::unitbuf;
    try {
        const Options options = parse_options(argc, argv);

        if (!options.info_path.empty()) {
            const bool is_surface
                = options.info_path.size() > 4
                && options.info_path.compare(options.info_path.size() - 4, 4, ".vts") == 0;
            if (is_surface)
                print_surface_info(options.info_path);
            else
                print_volume_info(options.info_path);
            return 0;
        }

        if (!options.swatch_colormap.empty()) {
            write_colormap_swatch(options.swatch_colormap, options.swatch_png, options.trim);
            return 0;
        }

        ospr::Script script = ospr::load_script(options.script_path);

        const std::filesystem::path directory = options.output_directory.empty()
            ? std::filesystem::path(script.output.directory)
            : std::filesystem::path(options.output_directory);
        std::filesystem::create_directories(directory);

        // ospInit consumes its own --osp: arguments and leaves ours alone.
        int ospray_argc = argc;
        const ospr::Device device(ospray_argc, const_cast<const char**>(argv));

        const auto load_start = std::chrono::steady_clock::now();
        ospr::FrameRenderer frame_renderer(script,
            script.output.width,
            script.output.height,
            script.session.renderer.samples_per_pixel);

        std::cout << "loaded scene in "
                  << std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - load_start)
                         .count()
                  << " s\n";

        const ospr::Bounds& bounds = frame_renderer.bounds();
        std::cout << "scene bounds  " << bounds.lo.x << " .. " << bounds.hi.x << "   "
                  << bounds.lo.y << " .. " << bounds.hi.y << "   " << bounds.lo.z << " .. "
                  << bounds.hi.z << "\n  centre " << bounds.center().x << ", "
                  << bounds.center().y << ", " << bounds.center().z << "   diagonal "
                  << bounds.diagonal() << "\n";

        const int total = ospr::frame_count(script);
        const int first = options.single_frame >= 0 ? options.single_frame : 0;
        const int last = options.single_frame >= 0 ? options.single_frame : total - 1;
        if (first < 0 || last >= total)
            throw std::runtime_error("frame index out of range (0.." + std::to_string(total - 1) + ")");

        for (int index = first; index <= last; ++index) {
            const auto frame_start = std::chrono::steady_clock::now();
            const float u = ospr::frame_to_param(script, index);
            const auto& pixels = frame_renderer.render(
                ospr::camera_for(script, u), ospr::opacity_at(script.keyframes, u));

            const std::string path = frame_path(directory, index);
            ospr::write_png_rgba(
                path, frame_renderer.width(), frame_renderer.height(), pixels.data());
            std::cout << "frame " << index << "/" << total - 1 << "  u=" << u << "  "
                      << std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - frame_start)
                             .count()
                      << " s  -> " << path << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ospr_render: " << error.what() << "\n";
        return 1;
    }
}
