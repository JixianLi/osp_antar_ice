#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "ospr/image.h"
#include "ospr/keyframe.h"
#include "ospr/render.h"
#include "ospr/script.h"

namespace {

struct Options
{
    std::string script_path;
    std::string output_directory;
    int single_frame{-1};
};

void print_usage()
{
    std::cerr << "usage: ospr_render <script.json> [--out DIR] [--frame N]\n"
                 "  --out DIR    override the script's output.dir\n"
                 "  --frame N    render only frame N\n";
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
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            std::exit(0);
        } else if (options.script_path.empty()) {
            options.script_path = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }
    if (options.script_path.empty())
        throw std::runtime_error("no script given");
    return options;
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
    try {
        const Options options = parse_options(argc, argv);
        const ospr::Script script = ospr::load_script(options.script_path);

        const std::filesystem::path directory = options.output_directory.empty()
            ? std::filesystem::path(script.output.directory)
            : std::filesystem::path(options.output_directory);
        std::filesystem::create_directories(directory);

        // ospInit consumes its own --osp: arguments and leaves ours alone.
        int ospray_argc = argc;
        const ospr::Device device(ospray_argc, const_cast<const char**>(argv));

        ospr::FrameRenderer frame_renderer(script,
            script.output.width,
            script.output.height,
            script.output.samples_per_pixel);

        const int total = ospr::frame_count(script);
        const int first = options.single_frame >= 0 ? options.single_frame : 0;
        const int last = options.single_frame >= 0 ? options.single_frame : total - 1;
        if (first < 0 || last >= total)
            throw std::runtime_error("frame index out of range (0.." + std::to_string(total - 1) + ")");

        for (int index = first; index <= last; ++index) {
            const float time_seconds = ospr::frame_time(script, index);
            const auto& pixels = frame_renderer.render(
                ospr::camera_at(script.keyframes, time_seconds));

            const std::string path = frame_path(directory, index);
            ospr::write_png_rgba(
                path, frame_renderer.width(), frame_renderer.height(), pixels.data());
            std::cout << "frame " << index << "/" << total - 1 << "  t=" << time_seconds
                      << "s  -> " << path << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ospr_render: " << error.what() << "\n";
        return 1;
    }
}
