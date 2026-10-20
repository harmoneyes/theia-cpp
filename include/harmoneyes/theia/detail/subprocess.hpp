#pragma once

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
// Both of these before <windows.h>, or every consumer of the umbrella header
// loses std::min/std::max as function names and gains ERROR, near and small
// as macros.
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  if defined(__APPLE__)
// environ is not linkable from a dylib on macOS; this is the sanctioned way.
#    include <crt_externs.h>
#    define HARMONEYES_THEIA_ENVIRON (*_NSGetEnviron())
#  else
extern char** environ;
#    define HARMONEYES_THEIA_ENVIRON environ
#  endif
#endif

// A child process with pipes on stdin, stdout and stderr.
//
// Deliberately narrow: spawn, write lines, read lines, stop. It exists to host
// the webcam gaze sidecar and nothing else.
//
// Lifecycle contract, which the caller must honour because getting it wrong is
// a data race rather than a hang:
//
//   terminate()    -- kill and reap the child. Readers then see EOF.
//   join readers   -- the caller's job.
//   close_pipes()  -- only once no thread can still be reading.
//
// Closing a read end while another thread is blocked in read() on it does NOT
// wake that thread, and worse, frees the descriptor number for reuse while the
// reader still holds it. Child death is what produces EOF, so terminate()
// before joining is both necessary and sufficient.

namespace harmoneyes::theia::detail {

class Process {
public:
    Process() = default;
    ~Process() { terminate(); close_pipes(); }

    Process(const Process&)            = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&)                 = delete;
    Process& operator=(Process&&)      = delete;

    /// Launch. argv[0] is the executable, resolved against PATH.
    void start(const std::vector<std::string>& argv)
    {
        if (argv.empty()) { throw std::invalid_argument("Process::start: empty argv"); }
#if defined(_WIN32)
        start_windows(argv);
#else
        start_posix(argv);
#endif
    }

    [[nodiscard]] bool running() const noexcept
    {
#if defined(_WIN32)
        return proc_ != nullptr;
#else
        return pid_ > 0;
#endif
    }

    /// Write one line, appending the newline. False if the pipe is gone.
    bool write_line(const std::string& line)
    {
        const std::string payload = line + "\n";
#if defined(_WIN32)
        if (stdin_w_ == nullptr) { return false; }
        DWORD written = 0;
        return WriteFile(stdin_w_, payload.data(), static_cast<DWORD>(payload.size()),
                         &written, nullptr) != 0;
#else
        if (stdin_w_ < 0) { return false; }
        std::size_t sent = 0;
        while (sent < payload.size()) {
            const auto n = ::write(stdin_w_, payload.data() + sent, payload.size() - sent);
            if (n < 0) {
                if (errno == EINTR) { continue; }
                return false;
            }
            if (n == 0) { return false; }
            sent += static_cast<std::size_t>(n);
        }
        return true;
#endif
    }

    std::size_t read_stdout(char* buffer, std::size_t capacity) { return read_one(out_r_, buffer, capacity); }
    std::size_t read_stderr(char* buffer, std::size_t capacity) { return read_one(err_r_, buffer, capacity); }

    /// Close the child's stdin, which the line protocol treats as shutdown.
    void close_stdin() noexcept
    {
#if defined(_WIN32)
        if (stdin_w_ != nullptr) { CloseHandle(stdin_w_); stdin_w_ = nullptr; }
#else
        if (stdin_w_ >= 0) { ::close(stdin_w_); stdin_w_ = -1; }
#endif
    }

    /// Wait for exit, up to `timeout`. True if the child is gone.
    bool wait_for_exit(std::chrono::milliseconds timeout)
    {
#if defined(_WIN32)
        if (proc_ == nullptr) { return true; }
        if (WaitForSingleObject(proc_, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) {
            return false;
        }
        CloseHandle(proc_);
        proc_ = nullptr;
        return true;
#else
        if (pid_ <= 0) { return true; }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        // No portable timed waitpid, so poll. Short enough to be invisible,
        // long enough not to spin.
        for (;;) {
            int         status = 0;
            const pid_t done   = ::waitpid(pid_, &status, WNOHANG);
            if (done == pid_ || (done < 0 && errno == ECHILD)) { pid_ = -1; return true; }
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
#endif
    }

    /// Kill and reap. Leaves the pipes open so readers can drain to EOF.
    void terminate() noexcept
    {
        close_stdin();
#if defined(_WIN32)
        if (proc_ != nullptr) {
            TerminateProcess(proc_, 1);
            WaitForSingleObject(proc_, 2000);
            CloseHandle(proc_);
            proc_ = nullptr;
        }
#else
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            int status = 0;
            while (::waitpid(pid_, &status, 0) < 0 && errno == EINTR) { }
            pid_ = -1;
        }
#endif
    }

    /// Close the read ends. Only safe once no thread can still be reading.
    void close_pipes() noexcept
    {
#if defined(_WIN32)
        close_handle(out_r_);
        close_handle(err_r_);
#else
        close_fd(out_r_);
        close_fd(err_r_);
#endif
    }

private:
#if defined(_WIN32)
    static void close_handle(HANDLE& h) noexcept { if (h != nullptr) { CloseHandle(h); h = nullptr; } }

    std::size_t read_one(HANDLE h, char* buffer, std::size_t capacity)
    {
        if (h == nullptr) { return 0; }
        DWORD got = 0;
        if (ReadFile(h, buffer, static_cast<DWORD>(capacity), &got, nullptr) == 0) { return 0; }
        return got;
    }

    /// Quote one argument the way the MSVC runtime parses it back.
    static void append_arg(std::string& out, const std::string& arg)
    {
        out += '"';
        std::size_t backslashes = 0;
        for (const char c : arg) {
            if (c == '\\') { ++backslashes; continue; }
            if (c == '"') {
                // Backslashes before a quote must be doubled, then the quote escaped.
                out.append(backslashes * 2 + 1, '\\');
                out += '"';
            } else {
                out.append(backslashes, '\\');
                out += c;
            }
            backslashes = 0;
        }
        // Trailing backslashes must be doubled so they do not escape the
        // closing quote. A path ending in a separator hits this.
        out.append(backslashes * 2, '\\');
        out += '"';
    }

    static std::wstring widen(const std::string& utf8)
    {
        if (utf8.empty()) { return {}; }
        const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                               static_cast<int>(utf8.size()), nullptr, 0);
        if (needed <= 0) { throw std::runtime_error("Process::start: cannot widen command line"); }
        std::wstring wide(static_cast<std::size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                            wide.data(), needed);
        return wide;
    }

    void start_windows(const std::vector<std::string>& argv)
    {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE in_r = nullptr, out_w = nullptr, err_w = nullptr;

        const auto cleanup = [&] {
            close_handle(in_r); close_handle(out_w); close_handle(err_w);
            close_handle(stdin_w_); close_handle(out_r_); close_handle(err_r_);
        };

        if (CreatePipe(&in_r, &stdin_w_, &sa, 0) == 0 ||
            CreatePipe(&out_r_, &out_w, &sa, 0) == 0 ||
            CreatePipe(&err_r_, &err_w, &sa, 0) == 0) {
            cleanup();
            throw std::runtime_error("Process::start: CreatePipe failed");
        }
        // Only the child's ends are inheritable.
        SetHandleInformation(stdin_w_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_r_,   HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_r_,   HANDLE_FLAG_INHERIT, 0);

        std::string command;
        for (const auto& arg : argv) {
            if (!command.empty()) { command += ' '; }
            append_arg(command, arg);
        }

        std::wstring wide;
        try { wide = widen(command); } catch (...) { cleanup(); throw; }
        wide.push_back(L'\0');   // CreateProcessW may modify this buffer

        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = in_r;
        si.hStdOutput = out_w;
        si.hStdError  = err_w;
        PROCESS_INFORMATION pi{};

        const BOOL ok = CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        close_handle(in_r); close_handle(out_w); close_handle(err_w);
        if (ok == 0) {
            close_handle(stdin_w_); close_handle(out_r_); close_handle(err_r_);
            throw std::runtime_error("Process::start: cannot launch " + argv[0]);
        }
        CloseHandle(pi.hThread);
        proc_ = pi.hProcess;
    }

    HANDLE proc_    = nullptr;
    HANDLE stdin_w_ = nullptr;
    HANDLE out_r_   = nullptr;
    HANDLE err_r_   = nullptr;
#else
    static void close_fd(int& fd) noexcept { if (fd >= 0) { ::close(fd); fd = -1; } }

    std::size_t read_one(int fd, char* buffer, std::size_t capacity)
    {
        if (fd < 0) { return 0; }
        for (;;) {
            const auto n = ::read(fd, buffer, capacity);
            if (n > 0) { return static_cast<std::size_t>(n); }
            if (n == 0) { return 0; }                        // genuine EOF
            if (errno == EINTR) { continue; }                // a signal, not EOF
            return 0;
        }
    }

    void start_posix(const std::vector<std::string>& argv)
    {
        // Writing to a dead child would otherwise kill this process. A
        // process-global side effect from a header-only library, which is why
        // it is also stated in the README.
        static std::once_flag sigpipe_once;
        std::call_once(sigpipe_once, [] { ::signal(SIGPIPE, SIG_IGN); });

        int fds[6];
        for (int& fd : fds) { fd = -1; }
        int* const in_pipe  = fds;
        int* const out_pipe = fds + 2;
        int* const err_pipe = fds + 4;

        const auto close_all = [&] { for (int& fd : fds) { close_fd(fd); } };

        if (::pipe(in_pipe) != 0) { throw std::runtime_error("Process::start: pipe failed"); }
        if (::pipe(out_pipe) != 0) { close_all(); throw std::runtime_error("Process::start: pipe failed"); }
        if (::pipe(err_pipe) != 0) { close_all(); throw std::runtime_error("Process::start: pipe failed"); }

        // Set close-on-exec BEFORE the spawn, so a second Process started
        // concurrently cannot inherit these. The child is unaffected because
        // adddup2 clears the flag on the duplicated descriptor.
        for (int fd : fds) { ::fcntl(fd, F_SETFD, FD_CLOEXEC); }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
        posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
        posix_spawn_file_actions_addclose(&actions, err_pipe[0]);

        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& a : argv) { args.push_back(const_cast<char*>(a.c_str())); }
        args.push_back(nullptr);

        // posix_spawn rather than fork+exec: fork from a multithreaded process
        // is only safe if the child touches async-signal-safe calls, and this
        // sidesteps the question entirely.
        pid_t     child = -1;
        const int rc    = ::posix_spawnp(&child, args[0], &actions, nullptr,
                                         args.data(), HARMONEYES_THEIA_ENVIRON);
        posix_spawn_file_actions_destroy(&actions);

        close_fd(in_pipe[0]);
        close_fd(out_pipe[1]);
        close_fd(err_pipe[1]);

        if (rc != 0) {
            close_all();
            throw std::runtime_error("Process::start: cannot launch " + argv[0] +
                                     " (" + std::to_string(rc) + ")");
        }

        pid_     = child;
        stdin_w_ = in_pipe[1];
        out_r_   = out_pipe[0];
        err_r_   = err_pipe[0];
    }

    pid_t pid_     = -1;
    int   stdin_w_ = -1;
    int   out_r_   = -1;
    int   err_r_   = -1;
#endif
};

}  // namespace harmoneyes::theia::detail
