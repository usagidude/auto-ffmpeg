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
#include "process.h"

namespace fs = std::filesystem;

typedef std::map<std::string, std::string> config_map;

enum class media_info
{
    vcodec,
    acodec,
    achan
};

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

    if (config.at("outmode") == "local" ||
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
    else if (config.at("outmode") == "rsource") {
        fs::path output;

        output.assign(input);
        output.replace_filename(config.at("outdir"));
        if (!fs::exists(output))
            fs::create_directory(output);

        fs_mtx.unlock();
        return output;
    }
    else if (config.at("outmode") == "rlocal" || config.at("outmode") == "rabsolute") {
        fs::path inroot = config.at("argv").empty() ?
            os::this_process::directory().string() :
            config.at("argv");

        if (!fs::is_directory(inroot))
            inroot.remove_filename();

        std::string outtail(input.string().substr(inroot.string().size()));
        if (outtail.starts_with("\\"))
            outtail = outtail.substr(1);

        fs::path output = config.at("outmode") == "rlocal" ?
            os::this_process::directory().append(config.at("outdir")) :
            config.at("outdir");
        
        output.append(outtail);
        output.remove_filename();

        if (!fs::exists(output))
            fs::create_directories(output);

        fs_mtx.unlock();
        return output;
    }

    if (!single_path.empty()) {
        if (!fs::exists(single_path))
            fs::create_directories(single_path);
    }

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
        std::system(ffmpeg_cmd.c_str());
    }
    else {
        os::process ffmpeg(ffmpeg_cmd, config.at("window") == "hide");
        ffmpeg.wait_for_exit();
    }
}

static void recursive_directory_walk(
    const fs::path& dir,
    const std::vector<std::string>& exts,
    const config_map& config,
    const std::regex& filter,
    std::queue<fs::path>& queue)
{
    for (const fs::path& file : fs::directory_iterator(dir)) {
        if (fs::is_directory(file) && file.filename() != config.at("outdir"))
            recursive_directory_walk(file, exts, config, filter, queue);
        if (std::ranges::none_of(exts, [&](const auto& ext) { return file.extension() == ext; }))
            continue;
        if (config.at("infilter") != "." && !std::regex_search(file.string(), filter))
            continue;
        queue.push(file);
    }
}

static void batch_mode(const config_map& config, const std::set<std::string>& progress, const fs::path& targetdir)
{
    std::mutex queue_lock;
    std::queue<fs::path> file_queue;
    std::vector<std::thread> workers;
    std::vector<std::string> exts;
    std::regex filter(config.at("infilter"), std::regex_constants::icase);
    
    for (const auto& ext : std::views::split(config.at("inext"), '|'))
        exts.emplace_back(ext.begin(), ext.end());

    if (!config.at("outmode").starts_with("r"))
        for (const fs::path& file : fs::directory_iterator(targetdir)) {
            if (std::ranges::none_of(exts, [&](const auto& ext) { return file.extension() == ext; }))
                continue;
            if (config.at("infilter") != "." && !std::regex_search(file.string(), filter))
                continue;
            file_queue.push(file);
        }
    else
        recursive_directory_walk(targetdir, exts, config, filter, file_queue);
    
    for (int i = 0; i < std::stoi(config.at("count")); ++i) {
        workers.emplace_back([&] {
            const bool resume = config.at("resume") == "true";
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

                if (resume && progress.contains(file.string()))
                    continue;
                if (invcodec != "any" && get_media_info(file, media_info::vcodec) != invcodec)
                    continue;
                if (inacodec != "any" && get_media_info(file, media_info::acodec) != inacodec)
                    continue;
                if (inachan != "any" && get_media_info(file, media_info::achan) != inachan)
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

int main(int argc, char* argv[])
{
    auto config = load_config("config.txt");
    auto progress = load_progress();

    config.emplace("argc", std::to_string(argc));
    config.emplace("argv", argc > 1 ? argv[1] : "");

    std::cout << "Working..." << std::endl;

    if (argc > 1) {
        if (fs::is_directory(argv[1]))
            batch_mode(config, progress, argv[1]);
        else
            exec_ffmpeg(argv[1], config, true);
    }
    else {
        batch_mode(config, progress, os::this_process::directory());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}