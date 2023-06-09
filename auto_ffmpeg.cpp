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

namespace fs = std::filesystem;

typedef std::map<std::string, std::string> config_map;
typedef std::pair<std::string, std::vector<std::regex>> probe_match_section;

static auto get_probe_matchlist(const config_map& config)
{
    std::vector<probe_match_section> probe_matchlist;
    const auto& probe_sections = config.at("probe_match");
    if (probe_sections == ".")
        return probe_matchlist;

    for (const auto& section : std::views::split(probe_sections, '|')) {
        bool first = true;
        std::string stream;
        std::vector<std::regex> rx_strs;
        for (const auto& sub_section : std::views::split(section, ';')) {
            if (first) {
                stream.assign(sub_section.begin(), sub_section.end());
                first = false;
                continue;
            }
            rx_strs.emplace_back(sub_section.begin(), sub_section.end(),
                std::regex_constants::icase);
        }
        probe_matchlist.emplace_back(stream, rx_strs);
    }

    return probe_matchlist;
}

static auto exec_ffprobe_match(const fs::path& input, const std::vector<probe_match_section>& match_sections)
{
    static thread_local os::ipipe inbound_pipe;
    for (const auto& section : match_sections) {
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
    for (std::string line; std::getline(prog_file, line);) {
        prog.emplace(line);
    }
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

static auto create_outdir(const config_map& config, const fs::path& input)
{
    static std::mutex fs_mtx;
    static fs::path single_path;

    fs_mtx.lock();

    if (!single_path.empty()) {
        fs_mtx.unlock();
        return single_path;
    }

    if (config.at("recursive") == "true") {
        if (config.at("outmode") == "source") {
            fs::path output(input);
            output.replace_filename(config.at("outdir"));

            if (!fs::exists(output))
                fs::create_directory(output);

            fs_mtx.unlock();
            return output;
        }
        else if (config.at("outmode") == "local" || config.at("outmode") == "absolute") {
            auto inroot = config.at("argv").empty() ?
                os::this_process::directory() :
                fs::path(config.at("argv"));

            if (!fs::is_directory(inroot))
                inroot.remove_filename();

            std::string outtail(input.string().substr(inroot.native().size()));
            if (outtail.starts_with("\\"))
                outtail = outtail.substr(1);

            auto output = config.at("outmode") == "local" ?
                os::this_process::directory().append(config.at("outdir")) :
                fs::path(config.at("outdir"));

            output.append(outtail).remove_filename();

            if (!fs::exists(output))
                fs::create_directories(output);

            fs_mtx.unlock();
            return output;
        }
    }
    else if (config.at("outmode") == "local" ||
        (config.at("outmode") == "source" && config.at("argv").empty())) {
        single_path = os::this_process::directory().append(config.at("outdir"));
    }
    else if (config.at("outmode") == "source") {
        fs::path inpath(config.at("argv"));
        if (fs::is_directory(inpath))
            inpath.append(config.at("outdir"));
        else
            inpath.replace_filename(config.at("outdir"));
        single_path = inpath;
    }
    else if (config.at("outmode") == "absolute") {
        single_path = config.at("outdir");
    }

    if (!single_path.empty() && !fs::exists(single_path))
        fs::create_directories(single_path);

    fs_mtx.unlock();
    return single_path;
}

static void exec_ffmpeg(const fs::path& input, const config_map& config, bool local_exec = false)
{
    auto output = create_outdir(config, input);

    output.append(input.filename().string());

    if (config.at("outext") != "keep")
        output.replace_extension(config.at("outext"));

    const auto ffmpeg_cmd = std::vformat(
        config.at("cmd"),
        std::make_format_args(input.string(), output.string())
    );

    if (local_exec) {
        [[maybe_unused]]
        auto _ = std::system(ffmpeg_cmd.c_str());
    }
    else {
        os::process ffmpeg(ffmpeg_cmd, config.at("window") == "hide");
        ffmpeg.wait_for_exit();
    }
}

static void batch_mode(const config_map& config, const fs::path& targetdir)
{
    std::mutex queue_lock;
    std::queue<fs::path> file_queue;
    std::vector<std::thread> workers;
    std::vector<std::string> exts;
    const std::regex filter(config.at("infilter"), std::regex_constants::icase);
    const bool recursive = config.at("recursive") == "true";
    const auto progress = load_progress();

    for (const auto& ext : std::views::split(config.at("inext"), '|'))
        exts.emplace_back(ext.begin(), ext.end());

    const std::function<void(const fs::path&)> get_media_files = [&](const auto& dir) {
        for (const fs::path& path : fs::directory_iterator(dir)) {
            if (recursive && fs::is_directory(path) && path.filename() != config.at("outdir"))
                get_media_files(path);
            if (std::ranges::none_of(exts, [&](const auto& ext) { return path.extension() == ext; }))
                continue;
            if (config.at("infilter") != "." && !std::regex_search(path.string(), filter))
                continue;
            file_queue.push(path);
        }
    };
    get_media_files(targetdir);
    
    for (int i = 0; i < std::stoi(config.at("count")); ++i) {
        workers.emplace_back([&] {
            const bool resume = config.at("resume") == "true";
            const auto probe_matchlist = get_probe_matchlist(config);
            for (;;) {
                queue_lock.lock();
                if (file_queue.empty()) {
                    queue_lock.unlock();
                    break;
                }

                const fs::path file = std::move(file_queue.front());
                file_queue.pop();
                queue_lock.unlock();

                if (resume && progress.contains(file.string()))
                    continue;
                if (!probe_matchlist.empty() && !exec_ffprobe_match(file, probe_matchlist))
                    continue;

                exec_ffmpeg(file, config);
                if (resume)
                    save_progress(file);
            }
        });
    }

    for (auto& worker : workers)
        worker.join();
}

static auto load_config(const std::string& file)
{
    config_map out_map;
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
    auto config = load_config("config.txt");

    config.emplace("argc", std::to_string(argc));
    config.emplace("argv", argc > 1 ? argv[1] : "");

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