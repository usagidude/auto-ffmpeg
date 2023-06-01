#ifdef __gnu_linux__
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <array>
#else
#include <Windows.h>
#include <rpcdce.h>
#pragma warning( disable : 6031 )
#endif
#include <format>
#include "process.h"

namespace os {

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
        _process_handle(nullptr), _thread_handle(nullptr),
        _cmd(cmd), _hide(hide), _redirect(redirect),
        _stdout_rd(nullptr), _stdout_wr(nullptr)
    {
        if (redirect) {
            SECURITY_ATTRIBUTES sec_att{};
            UUID uuid;
            RPC_CSTR uuid_str;

            UuidCreateSequential(&uuid);
            UuidToStringA(&uuid, &uuid_str);

            const std::string pipe_name = std::format(R"(\\.\pipe\LOCAL\{})", reinterpret_cast<char*>(uuid_str));
            RpcStringFreeA(&uuid_str);

            sec_att.nLength = sizeof(SECURITY_ATTRIBUTES);
            sec_att.bInheritHandle = TRUE;

            _stdout_rd = CreateNamedPipeA(
                pipe_name.c_str(), PIPE_ACCESS_INBOUND,
                PIPE_NOWAIT, 1, MAXDWORD, MAXDWORD, 0, nullptr);
            _stdout_wr = CreateFileA(
                pipe_name.c_str(), GENERIC_WRITE, 0, &sec_att,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            SetHandleInformation(_stdout_rd, HANDLE_FLAG_INHERIT, 0);
        }
    }

    void process::start()
    {
        STARTUPINFOA start_info{};
        PROCESS_INFORMATION proc_info;
        start_info.cb = sizeof(start_info);
        if (_redirect) {
            start_info.hStdError = _stdout_wr;
            start_info.hStdOutput = _stdout_wr;
            start_info.hStdInput = INVALID_HANDLE_VALUE;
            start_info.dwFlags = STARTF_USESTDHANDLES;
        }
        else if (_hide) {
            start_info.dwFlags = STARTF_USESHOWWINDOW;
            start_info.wShowWindow = SW_HIDE;
        }
        CreateProcessA(NULL, _cmd.data(),
            NULL, NULL, _redirect,
            _redirect ? 0 : CREATE_NEW_CONSOLE,
            NULL, NULL, &start_info, &proc_info);
        _process_handle = proc_info.hProcess;
        _thread_handle = proc_info.hThread;
    }

    void process::wait_for_exit()
    {
        if (_process_handle) {
            WaitForSingleObject(_process_handle, INFINITE);
        }
    }

    void process::run()
    {
        start();
        wait_for_exit();
    }

    void process::get_stdout(std::string& out)
    {
        DWORD r;
        char stdout_buf[2048];

        if (_stdout_rd == nullptr)
            return;

        while (ReadFile(_stdout_rd, stdout_buf, 2048, &r, nullptr))
            out.append(stdout_buf, r);
    }

    process::~process()
    {
        if (_process_handle) {
            CloseHandle(_process_handle);
            CloseHandle(_thread_handle);
        }
        if (_stdout_rd) {
            CloseHandle(_stdout_rd);
            CloseHandle(_stdout_wr);
        }
    }

    pipe::pipe() : _stdout_rd(nullptr), _stdout_wr(nullptr)
    {
        SECURITY_ATTRIBUTES sec_att{};
        UUID uuid;
        RPC_CSTR uuid_str;

        UuidCreateSequential(&uuid);
        UuidToStringA(&uuid, &uuid_str);

        const std::string pipe_name = 
            std::format(R"(\\.\pipe\LOCAL\{})",
                        reinterpret_cast<char*>(uuid_str));
        RpcStringFreeA(&uuid_str);

        sec_att.nLength = sizeof(SECURITY_ATTRIBUTES);
        sec_att.bInheritHandle = TRUE;

        _stdout_rd = CreateNamedPipeA(
            pipe_name.c_str(), PIPE_ACCESS_INBOUND,
            PIPE_NOWAIT, 1, MAXDWORD, MAXDWORD, 0, nullptr);
        _stdout_wr = CreateFileA(
            pipe_name.c_str(), GENERIC_WRITE, 0, &sec_att,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        SetHandleInformation(_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    }

    void* pipe::native_handle()
    {
        return nullptr;
    }

    void pipe::read(std::string& out)
    {
    }

    pipe::~pipe()
    {
    }

#endif

}