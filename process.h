#pragma once
#include <string>

#ifdef __gnu_linux__
class process
{
private:
    int _pid;
    std::string _cmd;
    bool _hide;
public:
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
    static std::string get_exe_path();
    process(const std::string& cmd, bool hide);
    void start();
    void wait_for_exit();
    ~process();
};
#endif
