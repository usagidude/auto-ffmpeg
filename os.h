#pragma once
#include <string>
#include <filesystem>

#ifndef thread_local
# if __STDC_VERSION__ >= 201112 && !defined __STDC_NO_THREADS__
#  define thread_local _Thread_local
# elif defined _WIN32 && ( \
       defined _MSC_VER || \
       defined __ICL || \
       defined __DMC__ || \
       defined __BORLANDC__ )
#  define thread_local __declspec(thread) 
# elif defined __GNUC__ || \
       defined __SUNPRO_C || \
       defined __xlC__
#  define thread_local __thread
# else
#  error "Cannot define thread_local"
# endif
#endif

namespace os {

#ifdef __gnu_linux__
    class this_process
    {
    public:
        static std::filesystem::path path();
        static std::filesystem::path directory();
    };

    class ipipe
    {
    private:
        std::string _pipe;
    public:
        ipipe();
        const char* native_handle() const;
        void read(std::string& out) const;
        ~ipipe();
        operator const char* () const;
    };

    class process
    {
    private:
        int _pid;
        std::string _cmd;
        bool _hide;
        void init(const char* stdout_pipe);
    public:
        process(const std::string& cmd, bool hide = false);
        process(const std::string& cmd, const char* stdout_pipe);
        void wait_for_exit() const;
        ~process();
    };

#else

    class this_process
    {
    public:
        static std::filesystem::path path();
        static std::filesystem::path directory();
    };

    class ipipe
    {
    private:
        void* _stdout_rd;
        void* _stdout_wr;
    public:
        ipipe();
        void* native_handle() const;
        void read(std::string& out) const;
        ~ipipe();
        operator void*() const;
    };

    class process
    {
    private:
        void* _process_handle;
        void* _thread_handle;
        std::string _cmd;
        bool _hide;
        void init(void* stdout_pipe);
    public:
        process(const std::string& cmd, bool hide = false);
        process(const std::string& cmd, void* stdout_pipe);
        void wait_for_exit() const;
        ~process();
    };
#endif

}