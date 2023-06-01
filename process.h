#pragma once
#include <string>
#include <filesystem>

namespace os {

#ifdef __gnu_linux__
    class process
    {
    private:
        int _pid;
        std::string _cmd;
        bool _hide;
    public:
        static std::string get_exe_path();
        static std::string get_exe_directory();
        process(const std::string& cmd, bool hide);
        void start();
        void wait_for_exit();
        ~process();
    };

#else
    class pipe
    {
    private:
        void* _stdout_rd;
        void* _stdout_wr;
    public:
        pipe();
        void* native_handle() const;
        void read(std::string& out) const;
        ~pipe();
    };
    class process
    {
    private:
        void* _process_handle;
        void* _thread_handle;
        std::string _cmd;
        bool _hide;
    public:
        static std::filesystem::path get_exe_path();
        static std::filesystem::path get_exe_directory();
        process(const std::string& cmd, bool hide = false);
        void start();
        void start(const pipe& outpipe);
        void wait_for_exit();
        void run();
        void run(const pipe& outpipe);
        ~process();
    };
#endif

}