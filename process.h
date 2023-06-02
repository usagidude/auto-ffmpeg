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
        operator void*() const;
    };
    class process
    {
    private:
        void* _process_handle;
        void* _thread_handle;
        std::string _cmd;
        bool _hide;
        void init(const std::string& cmd, void* out_pipe);
    public:
        static std::filesystem::path get_exe_path();
        static std::filesystem::path get_exe_directory();
        process(const std::string& cmd, bool hide = false);
        process(const std::string& cmd, void* out_pipe);
        void wait_for_exit() const;
        ~process();
    };
#endif

}