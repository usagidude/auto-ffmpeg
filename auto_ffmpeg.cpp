#include <iostream>
#include <fstream>
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
#include "os.h"
#include "config.h"

namespace fs = std::filesystem;

class auto_ffmpeg : config_t
{
private:
    static auto load_progress()
    {
        std::set<fs::path> prog;
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

    auto create_outdir(const fs::path& input) const
    {
        static std::mutex fs_mtx;
        static fs::path single_path;

        fs_mtx.lock();

        if (!single_path.empty()) {
            fs_mtx.unlock();
            return single_path;
        }

        if (recursive) {
            if (omode == outmode::source) {
                fs::path output(input);
                output.replace_filename(outdir);

                if (!fs::exists(output))
                    fs::create_directory(output);

                fs_mtx.unlock();
                return output;
            }
            else if (omode == outmode::local || omode == outmode::absolute) {
                auto inroot = argv.empty() ?
                    os::this_process::directory() :
                    fs::path(argv);

                if (!fs::is_directory(inroot))
                    inroot.remove_filename();

                std::string outtail(input.string().substr(inroot.native().size()));
                if (outtail.starts_with("\\"))
                    outtail = outtail.substr(1);

                auto output = omode == outmode::local ?
                    os::this_process::directory().append(outdir) :
                    fs::path(outdir);

                output.append(outtail).remove_filename();

                if (!fs::exists(output))
                    fs::create_directories(output);

                fs_mtx.unlock();
                return output;
            }
        }
        else if (omode == outmode::local || (omode == outmode::source && argv.empty())) {
            single_path = os::this_process::directory().append(outdir);
        }
        else if (omode == outmode::source) {
            fs::path inpath(argv);
            if (fs::is_directory(inpath))
                inpath.append(outdir);
            else
                inpath.replace_filename(outdir);
            single_path = inpath;
        }
        else if (omode == outmode::absolute) {
            single_path = outdir;
        }

        if (!single_path.empty() && !fs::exists(single_path))
            fs::create_directories(single_path);

        fs_mtx.unlock();
        return single_path;
    }

    auto ffprobe_match(const fs::path& input) const
    {
        static thread_local os::ipipe inbound_pipe;
        for (const auto& section : probe_matches) {
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

    void get_media_files(const fs::path& dir, std::queue<fs::path>& file_queue) const
    {
        for (const fs::path& path : fs::directory_iterator(dir)) {
            if (recursive && fs::is_directory(path) && path.filename() != outdir)
                get_media_files(path, file_queue);
            if (!inexts.contains(path.extension()))
                continue;
            if (filter_by_name && !std::regex_search(path.string(), infilter))
                continue;
            file_queue.push(path);
        }
    }

public:
    auto_ffmpeg(const std::string& file, const char* argv, int argc) :
        config_t(file, argv, argc) { }

    void single_exec(const fs::path& input, bool local_exec = false) const
    {
        auto output = create_outdir(input);

        output.append(input.filename().string());

        if (outext != "keep")
            output.replace_extension(outext);

        const auto ffmpeg_cmd = 
            std::vformat(cmd, std::make_format_args(input.string(), output.string()));

        if (local_exec) {
            [[maybe_unused]]
            auto _ = std::system(ffmpeg_cmd.c_str());
        }
        else {
            os::process ffmpeg(ffmpeg_cmd, hide_window);
            ffmpeg.wait_for_exit();
        }
    }

    void batch_exec(const fs::path& targetdir) const
    {
        std::mutex queue_lock;
        std::queue<fs::path> file_queue;
        std::vector<std::thread> workers;
        const auto progress = load_progress();

        get_media_files(targetdir, file_queue);

        for (int i = 0; i < count; ++i) {
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

                    if (resume && progress.contains(file))
                        continue;
                    if (!probe_matches.empty() && !ffprobe_match(file))
                        continue;

                    single_exec(file);
                    if (resume)
                        save_progress(file);
                }
            });
        }

        for (auto& worker : workers)
            worker.join();
    }
};

int main(int argc, char* argv[])
{
    const auto_ffmpeg ffmpeg("config.txt", argc > 1 ? argv[1] : "", argc);

    std::cout << "Working..." << std::endl;

    if (argc > 1) {
        if (fs::is_directory(argv[1]))
            ffmpeg.batch_exec(argv[1]);
        else
            ffmpeg.single_exec(argv[1], true);
    }
    else {
        ffmpeg.batch_exec(os::this_process::directory());
    }

    std::cout << "Done. Exiting in 60 seconds..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(60));
}