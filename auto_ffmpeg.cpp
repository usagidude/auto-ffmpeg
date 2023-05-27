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
#include "process.h"

namespace fs = std::filesystem;

static std::map<std::string, std::string> load_config(const std::string& file)
{
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([^ >]+) *> *(.+)$");

    std::ifstream config_file(process::get_exe_directory().append(file));
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    return out_map;
}

static void exec_ffmpeg(const fs::path& input, std::map<std::string, std::string>& config, bool local_exec = false)
{
    fs::path output(config["outdir"]);

    output.append(input.filename().string()).
        replace_extension(config["outext"]);

    auto ffmpeg_cmd = std::vformat(
        config["cmd"],
        std::make_format_args(input.string(), output.string())
    );

    if (local_exec) {
        std::system(ffmpeg_cmd.c_str());
    }
    else {
        process ffmpeg(ffmpeg_cmd, config["window"] == "hide");
        ffmpeg.start();
        ffmpeg.wait_for_exit();
    }
}

static std::string get_video_codec(const fs::path& input)
{
    std::regex vid_rx("Stream #0:0.+Video: ([a-z0-9]+)", std::regex_constants::icase);
    std::smatch m;
    auto cmd = std::format("ffprobe \"{}\"", input.string());
    process proc(cmd, false, true);
    proc.start();
    proc.wait_for_exit();
    auto output = proc.get_stdout();
    std::regex_search(output, m, vid_rx);
    return m[1];
}

static void batch_mode(std::map<std::string, std::string>& config, const fs::path& targetdir)
{
    auto wk_idx = 0, wk_cnt = std::stoi(config["count"]);
    std::vector<std::vector<fs::path>> cmd_queues(wk_cnt);
    std::vector<std::thread> cmd_workers;
    std::vector<std::string> exts;
    std::string invcodec = config["invcodec"] == "any" ? "" : config["invcodec"];

    for (const auto& ext : std::views::split(config["inext"], '|'))
        exts.emplace_back(ext.begin(), ext.end());

    for (const fs::path& file : fs::directory_iterator(targetdir)) {
        if (std::ranges::none_of(exts, [&](auto& ext)
            { return file.extension() == ext; }))
            continue;
        if (!invcodec.empty() && invcodec != get_video_codec(file))
            continue;

        cmd_queues[wk_idx].push_back(file);
        wk_idx = (wk_idx + 1) % wk_cnt;
    }

    for (const auto& queue : cmd_queues) {
        cmd_workers.emplace_back(
            [&] { for (const auto& input : queue) exec_ffmpeg(input, config); }
        );
    }

    for (auto& t : cmd_workers)
        t.join();
}



int main(int argc, char* argv[])
{
    auto config = load_config("config.txt");

    if (config["outmode"] == "local") {
        config["outdir"] = process::get_exe_directory().append(config["outdir"]).string();
    }
    else if (config["outmode"] == "source" && argc > 1) {
        fs::path inpath(argv[1]);
        if (fs::is_directory(argv[1]))
            inpath.append(config["outdir"]);
        else
            inpath.replace_filename(config["outdir"]);
        config["outdir"] = inpath.string();
    }

    if (!fs::exists(config["outdir"]))
        fs::create_directory(config["outdir"]);

    std::cout << "Working..." << std::endl;

    if (argc > 1) {
        if (fs::is_directory(argv[1]))
            batch_mode(config, argv[1]);
        else
            exec_ffmpeg(argv[1], config, true);
    }
    else {
        batch_mode(config, process::get_exe_directory());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}