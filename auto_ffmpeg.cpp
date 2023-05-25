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

std::map<std::string, std::string> load_config(const std::string& file)
{
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([^ >]+) *> *(.+)$");

    std::ifstream config_file(file);
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    return out_map;
}

int main()
{
    auto config = load_config("config.txt");
    auto wk_idx = 0, wk_cnt = std::stoi(config["count"]);
    std::vector<std::vector<std::string>> cmd_queues(wk_cnt);
    std::vector<std::thread> cmd_workers;
    std::vector<std::string> exts;

    for (const auto& ext : std::views::split(config["inext"], '|'))
        exts.emplace_back(ext.begin(), ext.end());

    if (!fs::exists(config["outdir"]))
        fs::create_directory(config["outdir"]);

    for (const fs::path& in : fs::directory_iterator(fs::current_path())) {
        if (std::ranges::none_of(exts, [&](auto& ext)
            { return in.extension() == ext; }))
            continue;

        auto out = fs::path(config["outdir"]).
            append(in.filename().string()).
            replace_extension(config["outext"]);

        auto cmd = std::vformat(
            config["cmd"],
            std::make_format_args(in.filename().string(), out.string())
        );

        cmd_queues[wk_idx].push_back(cmd);

        wk_idx = (wk_idx + 1) % wk_cnt;
    }

    for (const auto& queue : cmd_queues) {
        cmd_workers.emplace_back([&] {
            bool hide = config["window"] == "hide";
            for (const auto& cmd : queue) {
                process ffmpeg(cmd, hide);
                ffmpeg.start();
                ffmpeg.wait_for_exit();
            }
        });
    }

    std::cout << "Working..." << std::endl;
    for (auto& t : cmd_workers) {
        t.join();
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}
