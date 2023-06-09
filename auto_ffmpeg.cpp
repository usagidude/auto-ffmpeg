#include <iostream>
#include <fstream>
#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <thread>
#include <chrono>
#include <filesystem>
#include <regex>
#include <format>
#include <mutex>
#include <queue>
#include <set>
#include <utility>
#include <functional>
#include "process.h"
#include "config.h"

namespace fs = std::filesystem;

static auto exec_ffprobe_match(const fs::path& input, const config_t& config)
{
    static thread_local os::ipipe inbound_pipe;
    for (const auto& section : config.probe_matches) {
        std::string output;
        os::process ffprobe(
            std::format(
                "ffprobe -hide_banner -i \"{}\" -show_streams -select_streams {}",
                input.string(), section.first),
            inbound_pipe
        );
        ffprobe.wait_for_exit();
        inbound_pipe.read(output);
        for (const auto& rx : section.second)
            if (!std::regex_search(output, rx))
                return false;
    }
    return true;
}

static auto load_progress()
{
    std::set<std::string> prog;
    const fs::path prog_path = os::this_process::directory().append("progress.txt");
    if (!fs::exists(prog_path))
        return prog;
    std::ifstream prog_file(prog_path);
    for (std::string line; std::getline(prog_file, line);)
        prog.emplace(line);
    prog_file.close();
    return prog;
}

static void save_progress(const fs::path& file)
{
    static std::mutex fs_mtx;

    fs_mtx.lock();

    std::ofstream prog_file(
        os::this_process::directory().append("progress.txt"),
        std::ios::app);
    prog_file << file.string() << std::endl;
    prog_file.close();

    fs_mtx.unlock();
}

static auto create_outdir(const config_t& config, const fs::path& input)
{
    static std::mutex fs_mtx;
    static fs::path single_path;

    fs_mtx.lock();

    if (!single_path.empty()) {
        fs_mtx.unlock();
        return single_path;
    }

    if (config.recursive) {
        if (config.omode == outmode::source) {
            fs::path output(input);
            output.replace_filename(config.outdir);

            if (!fs::exists(output))
                fs::create_directory(output);

            fs_mtx.unlock();
            return output;
        }
        else if (config.omode == outmode::local || config.omode == outmode::absolute) {
            auto inroot = config.argv.empty() ?
                os::this_process::directory() :
                fs::path(config.argv);

            if (!fs::is_directory(inroot))
                inroot.remove_filename();

            std::string outtail(input.string().substr(inroot.native().size()));
            if (outtail.starts_with("\\"))
                outtail = outtail.substr(1);

            auto output = config.omode == outmode::local ?
                os::this_process::directory().append(config.outdir) :
                fs::path(config.outdir);

            output.append(outtail).remove_filename();

            if (!fs::exists(output))
                fs::create_directories(output);

            fs_mtx.unlock();
            return output;
        }
    }
    else if (config.omode == outmode::local ||
        (config.omode == outmode::source && config.argv.empty())) {
        single_path = os::this_process::directory().append(config.outdir);
    }
    else if (config.omode == outmode::source) {
        fs::path inpath(config.argv);
        if (fs::is_directory(inpath))
            inpath.append(config.outdir);
        else
            inpath.replace_filename(config.outdir);
        single_path = inpath;
    }
    else if (config.omode == outmode::absolute) {
        single_path = config.outdir;
    }

    if (!single_path.empty() && !fs::exists(single_path))
        fs::create_directories(single_path);

    fs_mtx.unlock();
    return single_path;
}

static void exec_ffmpeg(const fs::path& input, const config_t& config,
    bool local_exec = false)
{
    auto output = create_outdir(config, input);

    output.append(input.filename().string());

    if (config.outext != "keep")
        output.replace_extension(config.outext);

    const auto ffmpeg_cmd = std::vformat(
        config.cmd,
        std::make_format_args(input.string(), output.string())
    );

    if (local_exec) {
        [[maybe_unused]]
        auto _ = std::system(ffmpeg_cmd.c_str());
    }
    else {
        os::process ffmpeg(ffmpeg_cmd, config.hide_window);
        ffmpeg.wait_for_exit();
    }
}

static void batch_mode(const config_t& config, const fs::path& targetdir)
{
    std::mutex queue_lock;
    std::queue<fs::path> file_queue;
    std::vector<std::thread> workers;
    const auto progress = load_progress();

    const std::function<void(const fs::path&)> get_media_files = [&](const auto& dir) {
        for (const fs::path& path : fs::directory_iterator(dir)) {
            if (config.recursive && fs::is_directory(path) && path.filename() != config.outdir)
                get_media_files(path);
            if (!config.inexts.contains(path.extension().string()))
                continue;
            if (config.filter_by_name && !std::regex_search(path.string(), config.infilter))
                continue;
            file_queue.push(path);
        }
    };
    get_media_files(targetdir);
    
    for (int i = 0; i < config.count; ++i) {
        workers.emplace_back([&] {
            for (;;) {
                queue_lock.lock();
                if (file_queue.empty()) {
                    queue_lock.unlock();
                    break;
                }

                const fs::path file = std::move(file_queue.front());
                file_queue.pop();
                queue_lock.unlock();

                if (config.resume && progress.contains(file.string()))
                    continue;
                if (!config.probe_matches.empty() && !exec_ffprobe_match(file, config))
                    continue;

                exec_ffmpeg(file, config);
                if (config.resume)
                    save_progress(file);
            }
        });
    }

    for (auto& worker : workers)
        worker.join();
}

static auto load_config_map(const std::string& file)
{
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([^ >]+) *> *(.+)$");

    std::ifstream config_file(os::this_process::directory().append(file));
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    return out_map;
}

int main(int argc, char* argv[])
{
    const config_t config(load_config_map("config.txt"), argc > 1 ? argv[1] : "", argc);

    std::cout << "Working..." << std::endl;

    if (argc > 1) {
        if (fs::is_directory(argv[1]))
            batch_mode(config, argv[1]);
        else
            exec_ffmpeg(argv[1], config, true);
    }
    else {
        batch_mode(config, os::this_process::directory());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}