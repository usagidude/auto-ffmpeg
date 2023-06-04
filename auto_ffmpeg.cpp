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
#include "process.h"

namespace fs = std::filesystem;

enum class media_info
{
    vcodec,
    acodec,
    achan
};

static std::map<std::string, std::string> load_config(const std::string& file)
{
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([a-z]+) *> *(.+)$");

    std::ifstream config_file(os::this_process::directory().append(file));
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    return out_map;
}

static std::string get_media_info(const fs::path& input, media_info info)
{
    __declspec(thread) static os::pipe out_pipe;
    std::regex rx;
    std::smatch m;
    std::string stream;
    std::string output;

    switch (info)
    {
    case media_info::vcodec:
    case media_info::acodec:
        rx.assign("^codec_name=([a-z0-9]+)", std::regex_constants::icase);
        break;
    case media_info::achan:
        rx.assign("^channels=([0-9])", std::regex_constants::icase);
        break;
    }

    switch (info)
    {
    case media_info::vcodec:
        stream.assign("v:0");
        break;
    case media_info::acodec:
    case media_info::achan:
        stream.assign("a:0");
        break;
    }

    os::process ffprobe(
        std::format(
            "ffprobe -hide_banner -i \"{}\" -show_streams -select_streams {}",
            input.string(), stream),
        out_pipe
    );
    ffprobe.wait_for_exit();
    out_pipe.read(output);

    return std::regex_search(output, m, rx) ? m[1] : std::string();
}

static void exec_ffmpeg(const fs::path& input, const std::map<std::string, std::string>& config, bool local_exec = false)
{
    fs::path output(config.at("outdir"));

    output.append(input.filename().string());

    if (config.at("outext") != "keep")
        output.replace_extension(config.at("outext"));

    const auto ffmpeg_cmd = std::vformat(
        config.at("cmd"),
        std::make_format_args(input.string(), output.string())
    );

    if (local_exec) {
        std::system(ffmpeg_cmd.c_str());
    }
    else {
        os::process ffmpeg(ffmpeg_cmd, config.at("window") == "hide");
        ffmpeg.wait_for_exit();
    }
}

static void batch_mode(const std::map<std::string, std::string>& config, const fs::path& targetdir)
{
    std::mutex queue_lock;
    std::queue<fs::path> file_queue;
    std::vector<std::thread> workers;
    std::vector<std::string> exts;
    std::regex filter(config.at("infilter"), std::regex_constants::icase);
    
    for (const auto& ext : std::views::split(config.at("inext"), '|'))
        exts.emplace_back(ext.begin(), ext.end());

    for (const fs::path& file : fs::directory_iterator(targetdir)) {
        if (std::ranges::none_of(exts, [&](const auto& ext) { return file.extension() == ext; }))
            continue;
        if (config.at("infilter") != "." && !std::regex_search(file.string(), filter))
            continue;
        file_queue.push(file);
    }

    for (int i = 0; i < std::stoi(config.at("count")); ++i) {
        workers.emplace_back([&] {
            const auto& invcodec = config.at("invcodec");
            const auto& inacodec = config.at("inacodec");
            const auto& inachan = config.at("inachan");
            for (;;) {
                queue_lock.lock();
                if (file_queue.empty()) {
                    queue_lock.unlock();
                    break;
                }

                const fs::path file = std::move(file_queue.front());
                file_queue.pop();
                queue_lock.unlock();

                if (invcodec != "any" && get_media_info(file, media_info::vcodec) != invcodec)
                    continue;
                if (inacodec != "any" && get_media_info(file, media_info::acodec) != inacodec)
                    continue;
                if (inachan != "any" && get_media_info(file, media_info::achan) != inachan)
                    continue;

                exec_ffmpeg(file, config);
            }
        });
    }

    for (auto& worker : workers)
        worker.join();
}

int main(int argc, char* argv[])
{
    auto config = load_config("config.txt");

    if (config["outmode"] == "local") {
        config["outdir"] = os::this_process::directory().append(config["outdir"]).string();
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
        batch_mode(config, os::this_process::directory());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}