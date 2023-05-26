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
    fs::path wdir(process::get_exe_path());
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([^ >]+) *> *(.+)$");

    std::ifstream config_file(wdir.replace_filename(file));
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    out_map["outdir"] = wdir.replace_filename(out_map["outdir"]).string();
    return out_map;
}

static void exec_ffmpeg(const std::string& cmd,
    const fs::path& in, fs::path out,
    const std::string& outext, bool hide_window)
{
    out.append(in.filename().string()).replace_extension(outext);
    process ffmpeg(
        std::vformat(cmd, std::make_format_args(in.string(), out.string())),
        hide_window
    );
    ffmpeg.start();
    ffmpeg.wait_for_exit();
}

static void batch_mode(std::map<std::string, std::string>& config, const fs::path& targetdir)
{
    auto wk_idx = 0, wk_cnt = std::stoi(config["count"]);
    std::vector<std::vector<std::string>> cmd_queues(wk_cnt);
    std::vector<std::thread> cmd_workers;
    std::vector<std::string> exts;

    for (const auto& ext : std::views::split(config["inext"], '|'))
        exts.emplace_back(ext.begin(), ext.end());

    for (const fs::path& file : fs::directory_iterator(targetdir)) {
        if (std::ranges::none_of(exts, [&](auto& ext)
            { return file.extension() == ext; }))
            continue;
        cmd_queues[wk_idx].push_back(file.string());
        wk_idx = (wk_idx + 1) % wk_cnt;
    }

    for (const auto& queue : cmd_queues) {
        cmd_workers.emplace_back([&] {
            for (const auto& file : queue) {
                exec_ffmpeg(config["cmd"],
                    file,
                    config["outdir"],
                    config["outext"],
                    config["window"] == "hide"
                );
            }
        });
    }

    for (auto& t : cmd_workers)
        t.join();
}

int main(int argc, char* argv[])
{
    auto config = load_config("config.txt");

    if (!fs::exists(config["outdir"]))
        fs::create_directory(config["outdir"]);

    std::cout << "Working..." << std::endl;

    if (argc > 1) {
        if (fs::is_directory(argv[1])) {
            batch_mode(config, argv[1]);
        }
        else {
            exec_ffmpeg(config["cmd"],
                argv[1],
                config["outdir"],
                config["outext"],
                config["window"] == "hide"
            );
        }
    }
    else {
        batch_mode(config, fs::path(process::get_exe_path()).remove_filename());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}