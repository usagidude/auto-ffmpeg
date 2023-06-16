#ifdef __gnu_linux__
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <uuid/uuid.h>
#else
#include <Windows.h>
#include <rpcdce.h>
#pragma warning( disable : 6031 )
#endif
#include <array>
#include <fstream>
#include <format>
#include "os.h"

namespace os {

#ifdef __gnu_linux__

    namespace this_process {
        std::filesystem::path path()
        {
            return std::filesystem::canonical("/proc/self/exe");
        }
        std::filesystem::path directory()
        {
            return path().remove_filename();
        }
    }

    void process::init(const char* stdout_pipe)
    {
        posix_spawnattr_t spawn_att;
        std::array<std::string, 3> argv_buf {
            "sh",
            "-c",
            std::format("{} 0>/dev/null 1>{} 2>/dev/null", _cmd, stdout_pipe ? stdout_pipe : "/dev/null")
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

    process::process(const std::string& cmd, bool hide) :
        _pid(0), _cmd(cmd), _hide(hide)
    {
        init(nullptr);
    }

    process::process(const std::string& cmd, const char* stdout_pipe) :
        _pid(0), _cmd(cmd), _hide(false)
    {
        init(stdout_pipe);
    }

    void process::wait_for_exit() const
    {
        int status;
        if (_pid) {
            waitpid(_pid, &status, 0);
        }
    }

    process::~process()
    {
    }

    ipipe::ipipe()
    {
        uuid_t uuid;
        char uuid_str[37];
        uuid_generate(uuid);
        uuid_unparse_lower(uuid, uuid_str);
        _pipe = (this_process::directory() / uuid_str).string();
    }

    const char* ipipe::native_handle() const
    {
        return _pipe.c_str();
    }

    void ipipe::read(std::string& out) const
    {
        std::ifstream pipe_file(_pipe);
        std::getline(pipe_file, out, '\0');
    }

    ipipe::operator const char* () const
    {
        return native_handle();
    }

    ipipe::~ipipe()
    {
        std::filesystem::remove(_pipe);
    }

#else

    namespace this_process {
        std::filesystem::path path()
        {
            static thread_local std::string exe(MAX_PATH, 0);
            if (!exe.starts_with("\0"))
                return exe;
            GetModuleFileNameA(nullptr, exe.data(), MAX_PATH);
            return exe;
        }
        std::filesystem::path directory()
        {
            return path().remove_filename();
        }
    }

    void process::init(void* stdout_pipe)
    {
        STARTUPINFOA start_info{};
        PROCESS_INFORMATION proc_info;
        start_info.cb = sizeof(start_info);
        if (stdout_pipe) {
            start_info.hStdError = stdout_pipe;
            start_info.hStdOutput = stdout_pipe;
            start_info.hStdInput = INVALID_HANDLE_VALUE;
            start_info.dwFlags = STARTF_USESTDHANDLES;
        }
        else if (_hide) {
            start_info.dwFlags = STARTF_USESHOWWINDOW;
            start_info.wShowWindow = SW_HIDE;
        }
        CreateProcessA(NULL, _cmd.data(),
            NULL, NULL, stdout_pipe ? TRUE : FALSE,
            stdout_pipe ? 0 : CREATE_NEW_CONSOLE,
            NULL, NULL, &start_info, &proc_info);
        _process_handle = proc_info.hProcess;
        _thread_handle = proc_info.hThread;
    }

    process::process(const std::string& cmd, bool hide) :
        _process_handle(nullptr), _thread_handle(nullptr),
        _cmd(cmd), _hide(hide)
    {
        init(nullptr);
    }

    process::process(const std::string& cmd, void* stdout_pipe) :
        _process_handle(nullptr), _thread_handle(nullptr),
        _cmd(cmd), _hide(false)
    {
        init(stdout_pipe);
    }

    void process::wait_for_exit() const
    {
        if (_process_handle) {
            WaitForSingleObject(_process_handle, INFINITE);
        }
    }

    process::~process()
    {
        if (_process_handle) {
            CloseHandle(_process_handle);
            CloseHandle(_thread_handle);
        }
    }

    ipipe::ipipe() : _stdout_rd(nullptr), _stdout_wr(nullptr)
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
            PIPE_NOWAIT, 1, 1 << 26, 1 << 26, 0, nullptr);
        _stdout_wr = CreateFileA(
            pipe_name.c_str(), GENERIC_WRITE, 0, &sec_att,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        SetHandleInformation(_stdout_rd, HANDLE_FLAG_INHERIT, 0);
    }

    void* ipipe::native_handle() const
    {
        return _stdout_wr;
    }

    void ipipe::read(std::string& out) const
    {
        DWORD r;
        char buffer[2048];

        if (_stdout_rd == nullptr)
            return;

        while (ReadFile(_stdout_rd, buffer, 2048, &r, nullptr))
            out.append(buffer, r);
    }
    
    ipipe::operator void* () const
    {
        return _stdout_wr;
    }

    ipipe::~ipipe()
    {
        if (_stdout_rd) {
            CloseHandle(_stdout_rd);
            CloseHandle(_stdout_wr);
        }
    }

#endif

}