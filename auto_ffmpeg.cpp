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

template<typename T>
class concurrent_queue : public std::queue<T>
{
private:
    std::mutex _lock;
public:
    bool safe_pop(T& val)
    {
        _lock.lock();
        if (this->empty()) {
            _lock.unlock();
            return false;
        }
        val = this->front();
        this->pop();
        _lock.unlock();
        return true;
    }
};

static std::map<std::string, std::string> load_config(const std::string& file)
{
    std::map<std::string, std::string> out_map;
    std::regex config_rx("^ *([a-z]+) *> *(.+)$");

    std::ifstream config_file(process::get_exe_directory().append(file));
    for (std::string line; std::getline(config_file, line);) {
        std::smatch m;
        std::regex_match(line, m, config_rx);
        out_map.emplace(m[1], m[2]);
    }
    config_file.close();

    return out_map;
}

static std::string get_video_codec(const fs::path& input)
{
    std::regex vid_rx("Stream #0:0.+Video: ([a-z0-9]+)", std::regex_constants::icase);
    std::smatch m;
    std::string output;

    process ffprobe(
        std::format("ffprobe \"{}\"", input.string()),
        false, true
    );
    ffprobe.run();
    ffprobe.get_stdout(output);

    return std::regex_search(output, m, vid_rx) ? m[1] : std::string();
}

static void exec_ffmpeg(const fs::path& input, std::map<std::string, std::string>& config, bool local_exec = false)
{
    fs::path output(config["outdir"]);

    output.append(input.filename().string());

    if (config["outext"] != "keep")
        output.replace_extension(config["outext"]);

    auto ffmpeg_cmd = std::vformat(
        config["cmd"],
        std::make_format_args(input.string(), output.string())
    );

    if (local_exec) {
        std::system(ffmpeg_cmd.c_str());
    }
    else {
        process ffmpeg(ffmpeg_cmd, config["window"] == "hide");
        ffmpeg.run();
    }
}

static void batch_mode(std::map<std::string, std::string>& config, const fs::path& targetdir)
{
    concurrent_queue<fs::path> vid_files;
    std::vector<std::thread> cmd_workers;
    std::vector<std::string> exts;

    for (const auto& ext : std::views::split(config["inext"], '|'))
        exts.emplace_back(ext.begin(), ext.end());

    for (const fs::path& file : fs::directory_iterator(targetdir)) {
        if (std::ranges::none_of(exts, [&](auto& ext) { return file.extension() == ext; }))
            continue;
        vid_files.push(file);
    }

    for (int i = 0; i < std::stoi(config["count"]); ++i) {
        cmd_workers.emplace_back([&] {
            const auto& invcodec = config["invcodec"];
            for (;;) {
                fs::path file;
                if (!vid_files.safe_pop(file))
                    break;
                if (invcodec != "any" && get_video_codec(file) != invcodec)
                    continue;
                exec_ffmpeg(file, config);
            }
        });
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