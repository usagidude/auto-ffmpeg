#pragma once
#include <string>
#include <filesystem>

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
class process
{
private:
    void* _hProcess;
    void* _hThread;
    std::string _cmd;
    bool _hide;
public:
    static std::filesystem::path get_exe_path();
    static std::filesystem::path get_exe_directory();
    process(const std::string& cmd, bool hide);
    void start();
    void wait_for_exit();
    ~process();
};
#endif
