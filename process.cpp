#ifdef __gnu_linux__
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <array>
#else
#include <Windows.h>
#endif
#include "process.h"

#ifdef __gnu_linux__

std::filesystem::path process::get_exe_path()
{
    return std::filesystem::canonical("/proc/self/exe");
}

std::filesystem::path process::get_exe_directory()
{
    return get_exe_path().remove_filename();
}

process::process(const std::string& cmd, bool hide) :
    _pid(0), _cmd(cmd), _hide(hide) {}

void process::start()
{
    posix_spawnattr_t spawn_att;
    std::array<std::string, 3> argv_buf{
      "sh",
      "-c",
      _cmd + " 0>/dev/null 1>/dev/null 2>/dev/null"
    };
    std::array<char*, 4> argv{
      argv_buf[0].data(),
      argv_buf[1].data(),
      argv_buf[2].data(),
      nullptr
    };
    posix_spawnattr_init(&spawn_att);
    posix_spawnattr_setflags(&spawn_att, POSIX_SPAWN_SETSID);
    posix_spawnp(&_pid, argv[0], nullptr, &spawn_att, argv.data(), environ);
    posix_spawnattr_destroy(&spawn_att);

}

void process::wait_for_exit()
{
    int status;
    if (_pid) {
        waitpid(_pid, &status, 0);
    }
}

process::~process()
{
}
#else

std::filesystem::path process::get_exe_path()
{
    std::string exe(MAX_PATH, 0);
    GetModuleFileNameA(nullptr, exe.data(), MAX_PATH);
    return exe;
}

std::filesystem::path process::get_exe_directory()
{
    return get_exe_path().remove_filename();
}

process::process(const std::string& cmd, bool hide, bool redirect) :
    _hProcess(nullptr), _hThread(nullptr),
    _cmd(cmd), _hide(hide), _redirect(redirect),
    stdout_rd(nullptr), stdout_wr(nullptr)
{
    if (redirect) {
        SECURITY_ATTRIBUTES sec_att{};
        sec_att.nLength = sizeof(SECURITY_ATTRIBUTES);
        sec_att.bInheritHandle = TRUE;
        sec_att.lpSecurityDescriptor = NULL;
        CreatePipe(&stdout_rd, &stdout_wr, &sec_att, 0);
        SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    }
}

void process::start()
{
    STARTUPINFOA start_info{};
    PROCESS_INFORMATION proc_info;
    start_info.cb = sizeof(start_info);
    if (_redirect) {
        start_info.hStdError = stdout_wr;
        start_info.hStdOutput = stdout_wr;
        start_info.hStdInput = INVALID_HANDLE_VALUE;
        start_info.dwFlags = STARTF_USESTDHANDLES;
    }
    else if (_hide) {
        start_info.dwFlags = STARTF_USESHOWWINDOW;
        start_info.wShowWindow = SW_HIDE;
    }
    CreateProcessA(NULL, _cmd.data(),
        NULL, NULL, _redirect,
        _redirect ? 0:CREATE_NEW_CONSOLE,
        NULL, NULL, &start_info, &proc_info);
    _hProcess = proc_info.hProcess;
    _hThread = proc_info.hThread;
}

void process::wait_for_exit()
{
    if (_hProcess) {
        WaitForSingleObject(_hProcess, INFINITE);
    }
}

std::string process::get_stdout()
{
    if (!stdout_rd)
        return std::string();
    __declspec(thread) static char stdout_buf[MAXINT16];
    DWORD r = 0;
    if (ReadFile(stdout_rd, stdout_buf, MAXINT16, &r, nullptr)) {

    }
    return std::string(stdout_buf, r);
}


process::~process()
{
    if (_hProcess) {
        CloseHandle(_hProcess);
        CloseHandle(_hThread);
    }
    if (stdout_rd) {
        CloseHandle(stdout_rd);
        CloseHandle(stdout_wr);
    }
}
#endif
