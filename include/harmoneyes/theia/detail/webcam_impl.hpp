#pragma once

// Definitions for <harmoneyes/theia_webcam.hpp>: a child process with stdio
// pipes, the sidecar's line protocol, and the tracker itself. Not part of the
// API, and not meant to be included on its own.

#ifndef HARMONEYES_THEIA_INCLUDING_WEBCAM_IMPL
#  error "Include <harmoneyes/theia_webcam.hpp> instead of this file."
#endif

#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <locale>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// ── Number parsing ──

// Locale-independent number parsing.
//
// Two reasons this exists rather than calling std::stod.
//
// std::stod honours LC_NUMERIC. Under a comma-decimal locale (de_DE, fr_FR and
// many others) "1.5" parses as 1.0, silently, with no error anywhere. The wire
// format this SDK reads is always C-locale, so a user's locale must not change
// how it is interpreted.
//
// std::from_chars for floating point is the right tool but is not universally
// available: libc++ only implemented it in LLVM 20, and macOS is a supported
// platform. So it is used where present and an explicitly classic-locale
// stringstream stands in where it is not.

namespace harmoneyes::theia::detail {

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
#  define HARMONEYES_THEIA_HAS_FP_FROM_CHARS 1
#else
#  define HARMONEYES_THEIA_HAS_FP_FROM_CHARS 0
#endif

/// Parse a complete C-locale decimal. Trailing characters make it fail.
[[nodiscard]] inline std::optional<double> parse_double(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.remove_prefix(1); }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (text.empty()) { return std::nullopt; }

#if HARMONEYES_THEIA_HAS_FP_FROM_CHARS
    double     value  = 0.0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) { return std::nullopt; }
    return value;
#else
    std::istringstream in{std::string{text}};
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail() || in.peek() != std::istringstream::traits_type::eof()) { return std::nullopt; }
    return value;
#endif
}

}  // namespace harmoneyes::theia::detail

// ── Sidecar line protocol ──

// The webcam sidecar speaks newline-delimited JSON over stdio. Every message
// is a flat object with known keys, so this is a scanner for that shape rather
// than a JSON library: a public header must not drag a JSON dependency into
// every consumer's translation unit.
//
// It does have to be a real scanner, though. Searching for `"x":` with a plain
// substring match also matches inside string VALUES, so a message like
//   {"type":"error","msg":"bad \"x\": 3"}
// mis-parses. Everything below tracks string state and escapes.

namespace harmoneyes::theia::detail::sidecar {

/// Append `value` to `out` as a JSON string, including the quotes.
///
/// Required, not optional: the init message carries a filesystem path and a
/// licence blob. Every Windows path contains backslashes, and interpolating
/// one raw produces invalid JSON.
inline void append_json_string(std::string& out, std::string_view value)
{
    out += '"';
    for (const char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                    out += kHex[static_cast<unsigned char>(c) & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

/// Decode a JSON string body (without the surrounding quotes).
[[nodiscard]] inline std::string unescape(std::string_view body)
{
    std::string out;
    out.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\' || i + 1 >= body.size()) { out += body[i]; continue; }
        switch (body[++i]) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                // Only the Basic Latin range is decoded; anything else is kept
                // verbatim rather than mangled, since these are diagnostics.
                if (i + 4 < body.size()) {
                    unsigned code = 0;
                    const auto* first = body.data() + i + 1;
                    if (std::from_chars(first, first + 4, code, 16).ec == std::errc{} && code < 0x80) {
                        out += static_cast<char>(code);
                        i += 4;
                        break;
                    }
                }
                out += "\\u";
                break;
            }
            default: out += '\\'; out += body[i];
        }
    }
    return out;
}

/// The raw text of `key`'s value in a flat JSON object, if present.
///
/// Only looks at the top level, and ignores anything inside a string.
[[nodiscard]] inline std::optional<std::string_view> raw_value(std::string_view line,
                                                               std::string_view key)
{
    bool        in_string = false;
    int         depth     = 0;
    std::size_t i         = 0;

    while (i < line.size()) {
        const char c = line[i];

        if (in_string) {
            if (c == '\\') { i += 2; continue; }
            if (c == '"')  { in_string = false; }
            ++i;
            continue;
        }

        if (c == '"') {
            // A string at depth 1 followed by ':' is a key.
            const std::size_t start = i + 1;
            std::size_t       end   = start;
            while (end < line.size()) {
                if (line[end] == '\\') { end += 2; continue; }
                if (line[end] == '"')  { break; }
                ++end;
            }
            if (end >= line.size()) { return std::nullopt; }

            std::size_t after = end + 1;
            while (after < line.size() && (line[after] == ' ' || line[after] == '\t')) { ++after; }

            const bool is_key = after < line.size() && line[after] == ':';
            if (is_key && depth == 1 && line.substr(start, end - start) == key) {
                std::size_t vs = after + 1;
                while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) { ++vs; }
                // Find the end of this value, honouring nesting and strings.
                std::size_t ve        = vs;
                int         vdepth    = 0;
                bool        vin_string = false;
                while (ve < line.size()) {
                    const char d = line[ve];
                    if (vin_string) {
                        if (d == '\\') { ve += 2; continue; }
                        if (d == '"')  { vin_string = false; }
                    } else if (d == '"') {
                        vin_string = true;
                    } else if (d == '{' || d == '[') {
                        ++vdepth;
                    } else if (d == '}' || d == ']') {
                        if (vdepth == 0) { break; }
                        --vdepth;
                    } else if (d == ',' && vdepth == 0) {
                        break;
                    }
                    ++ve;
                }
                // Trim trailing space so `{"valid": true }` parses. JSON.stringify
                // never emits it, but a hand-written producer or stub will.
                while (ve > vs && (line[ve - 1] == ' ' || line[ve - 1] == '\t')) { --ve; }
                return line.substr(vs, ve - vs);
            }

            if (is_key) { i = after + 1; } else { i = end + 1; }
            continue;
        }

        if (c == '{' || c == '[') { ++depth; }
        else if (c == '}' || c == ']') { --depth; }
        ++i;
    }
    return std::nullopt;
}

/// A string-valued field, unescaped.
[[nodiscard]] inline std::optional<std::string> string_field(std::string_view line,
                                                             std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw || raw->size() < 2 || raw->front() != '"' || raw->back() != '"') { return std::nullopt; }
    return unescape(raw->substr(1, raw->size() - 2));
}

/// A number-valued field.
///
/// Goes through detail::parse_double, which is locale-independent (the wire
/// format is always C-locale) and works on standard libraries whose
/// std::from_chars does not yet cover floating point.
[[nodiscard]] inline std::optional<double> number_field(std::string_view line,
                                                        std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw) { return std::nullopt; }
    return parse_double(*raw);
}

/// A boolean-valued field.
[[nodiscard]] inline std::optional<bool> bool_field(std::string_view line, std::string_view key)
{
    const auto raw = raw_value(line, key);
    if (!raw) { return std::nullopt; }
    if (*raw == "true")  { return true; }
    if (*raw == "false") { return false; }
    return std::nullopt;
}

/// Format a number for the wire, independent of the process locale.
///
/// std::to_string(double) honours LC_NUMERIC, so under a comma-decimal locale
/// it writes "75,000000", which is not valid JSON.
[[nodiscard]] inline std::string format_number(double value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(17);
    out << value;
    return out.str();
}

/// Decode the Tobii licence endpoint's UTF-16LE response.
///
/// The licence is Basic Latin, so each code unit is one ASCII byte and a zero.
/// Any other content returns empty rather than a mangled licence. Surrounding
/// whitespace and a byte-order mark are dropped.
[[nodiscard]] inline std::string decode_utf16le(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() % 2 != 0) { return {}; }
    std::size_t i = 0;
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) { i = 2; }

    std::string out;
    out.reserve(bytes.size() / 2);
    for (; i + 1 < bytes.size(); i += 2) {
        if (bytes[i + 1] != 0) { return {}; }
        if (bytes[i] != 0) { out += static_cast<char>(bytes[i]); }
    }
    const auto first = out.find_first_not_of(" \t\r\n");
    const auto last  = out.find_last_not_of(" \t\r\n");
    return (first == std::string::npos) ? std::string{} : out.substr(first, last - first + 1);
}

}  // namespace harmoneyes::theia::detail::sidecar

// ── Child process ──

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
#  include <pthread.h>
#  include <ctime>
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

#if !defined(_WIN32)
/// Stops a write to a pipe whose reader has exited from raising SIGPIPE,
/// without touching the process-wide signal disposition.
///
/// On macOS the pipe itself is marked F_SETNOSIGPIPE when it is created, so
/// this does nothing there. Elsewhere SIGPIPE is blocked for the writing
/// thread only, and if the write then fails with EPIPE, the signal it raised
/// is discarded before the mask is restored.
class SigpipeGuard {
public:
#  if defined(__APPLE__)
    void consume() noexcept {}
#  else
    SigpipeGuard() noexcept
    {
        sigemptyset(&pipe_);
        sigaddset(&pipe_, SIGPIPE);
        sigset_t pending;
        sigemptyset(&pending);
        sigpending(&pending);
        was_pending_ = sigismember(&pending, SIGPIPE) == 1;
        blocked_     = pthread_sigmask(SIG_BLOCK, &pipe_, &previous_) == 0;
    }
    ~SigpipeGuard() { if (blocked_) { pthread_sigmask(SIG_SETMASK, &previous_, nullptr); } }

    SigpipeGuard(const SigpipeGuard&)            = delete;
    SigpipeGuard& operator=(const SigpipeGuard&) = delete;

    /// Call after a write fails with EPIPE.
    void consume() noexcept
    {
        // A SIGPIPE that was already pending belongs to someone else.
        if (!blocked_ || was_pending_) { return; }
        const timespec zero{0, 0};
        while (sigtimedwait(&pipe_, nullptr, &zero) == -1 && errno == EINTR) { }
    }

private:
    sigset_t pipe_{};
    sigset_t previous_{};
    bool     was_pending_ = false;
    bool     blocked_     = false;
#  endif
};
#endif

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
        SigpipeGuard guard;
        std::size_t  sent = 0;
        while (sent < payload.size()) {
            const auto n = ::write(stdin_w_, payload.data() + sent, payload.size() - sent);
            if (n < 0) {
                if (errno == EINTR) { continue; }
                if (errno == EPIPE) { guard.consume(); }
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
#  if defined(F_SETNOSIGPIPE)
        // Writing to an exited child fails with EPIPE instead of raising
        // SIGPIPE. See SigpipeGuard for other platforms.
        ::fcntl(in_pipe[1], F_SETNOSIGPIPE, 1);
#  endif

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

// ── Webcam samples ──

namespace harmoneyes::theia::detail {

/// One gaze reading from the webcam gaze engine. Coordinates are normalised
/// to [0, 1] with the origin at the top-left of the screen.
struct WebcamGaze {
    double x            = 0.0;
    double y            = 0.0;
    bool   valid        = false;
    double timestamp_ms = 0.0;  ///< milliseconds since the Unix epoch
};

/// Screen geometry attached to every webcam sample. The resolution only
/// scales normalised gaze to pixels, so any fixed value gives the same
/// physical geometry; 1920x1080 matches the Python SDK.
struct WebcamGeometry {
    int screen_res_width_px  = 1920;
    int screen_res_height_px = 1080;
    int screen_width_mm      = 336;
    int screen_height_mm     = 210;
};

/// Turn a webcam gaze reading into the sample shape the webcam pipeline
/// expects.
///
/// The pipeline computes every angular feature from gaze_x_norm/gaze_y_norm.
/// With only pixel coordinates filled the sample still counts as valid, so
/// the stream looks healthy while every angular feature degrades to
/// NON_COMPUTE_VALUE. Setting those two is the point of this function.
[[nodiscard]] inline GazeSample build_webcam_sample(const WebcamGaze& gaze,
                                                    const WebcamGeometry& geometry) noexcept
{
    // The library treats a normalised gaze of exactly (0, 0) as "unset", so a
    // genuine reading at the top-left corner is moved off zero by one ULP.
    const auto nudge_off_zero = [](float value) {
        return value == 0.0F ? std::nextafter(0.0F, 1.0F) : value;
    };

    GazeSample s;
    s.timestamp_ns = static_cast<std::int64_t>(gaze.timestamp_ms * 1.0e6);

    if (gaze.valid) {
        s.gaze_x_norm = nudge_off_zero(static_cast<float>(gaze.x));
        s.gaze_y_norm = nudge_off_zero(static_cast<float>(gaze.y));

        const auto px = static_cast<float>(gaze.x * geometry.screen_res_width_px);
        const auto py = static_cast<float>(gaze.y * geometry.screen_res_height_px);
        s.left_eye_x = s.right_eye_x = px;
        s.left_eye_y = s.right_eye_y = py;
    } else {
        // A lost frame reads as closed eyes, which is how the webcam blink
        // detector expects it.
        s.left_eye_x = s.right_eye_x = std::nanf("");
        s.left_eye_y = s.right_eye_y = std::nanf("");
        s.left_eye_validity = s.right_eye_validity = 0;
        s.left_eye_status   = s.right_eye_status   = 1;  // blink / closed
    }

    // A webcam reports no pupil size.
    s.pupil_diameter_left  = NON_COMPUTE_VALUE;
    s.pupil_diameter_right = NON_COMPUTE_VALUE;
    s.pupil_diameter       = NON_COMPUTE_VALUE;

    s.screen_width_mm      = static_cast<float>(geometry.screen_width_mm);
    s.screen_height_mm     = static_cast<float>(geometry.screen_height_mm);
    s.screen_res_width_px  = geometry.screen_res_width_px;
    s.screen_res_height_px = geometry.screen_res_height_px;
    return s;
}

}  // namespace harmoneyes::theia::detail

// ── NexusWebcamTracker ──

namespace harmoneyes::theia {

struct NexusWebcamTracker::Impl {
    explicit Impl(Config config) : config_(std::move(config)) {}
    ~Impl() { close(); }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;



    void start()
    {
        const std::lock_guard<std::mutex> lifecycle{lifecycle_};
        if (running_.exchange(true)) {
            throw std::logic_error("NexusWebcamTracker::start: already started");
        }

        ready_        = false;
        failed_       = false;
        stream_ended_ = false;
        { std::lock_guard<std::mutex> lock{ready_mutex_}; failure_.clear(); }
        { std::lock_guard<std::mutex> lock{stderr_mutex_}; stderr_lines_.clear(); }

        try {
            // Everything that can be checked locally is checked before the
            // sidecar starts, so a missing piece is a clear error here.
            const std::vector<std::string> command = resolve_command();
            resolved_tobii_dir_ = resolve_tobii_dir();
            resolved_license_   = resolve_license();
            process_.start(command);
        } catch (...) {
            running_ = false;
            throw;
        }

        reader_        = std::thread([this] { read_loop(); });
        stderr_reader_ = std::thread([this] { stderr_loop(); });

        if (!process_.write_line(init_message())) {
            stop_locked();
            throw std::runtime_error("NexusWebcamTracker: the webcam process exited during startup" +
                                     stderr_tail());
        }

        std::unique_lock<std::mutex> lock{ready_mutex_};
        const bool signalled = ready_cv_.wait_for(lock, config_.ready_timeout,
                                                  [this] { return ready_ || failed_; });
        if (!signalled || failed_) {
            const std::string why = failed_ ? failure_ : "timed out waiting for the camera";
            lock.unlock();
            stop_locked();
            throw std::runtime_error("NexusWebcamTracker: could not start (" + why + ")" +
                                     stderr_tail());
        }
    }

    [[nodiscard]] std::vector<GazeSample> get_buffered_data()
    {
        std::lock_guard<std::mutex> lock{buffer_mutex_};
        std::vector<GazeSample>     out{buffer_.begin(), buffer_.end()};
        buffer_.clear();
        return out;
    }

    [[nodiscard]] bool is_connected() const
    {
        return running_.load() && ready_.load() && !failed_.load() && !stream_ended_.load();
    }

    void close()
    {
        const std::lock_guard<std::mutex> lifecycle{lifecycle_};
        stop_locked();
    }

    static constexpr std::size_t kMaxBuffered = 4096;

    [[nodiscard]] static std::string env(const char* name)
    {
        const char* v = std::getenv(name);
        return (v != nullptr) ? std::string{v} : std::string{};
    }

    [[nodiscard]] std::filesystem::path sidecar_dir() const
    {
        if (!config_.sidecar_dir.empty()) { return config_.sidecar_dir; }
        if (std::string dir = env("THEIA_WEBCAM_SIDECAR_DIR"); !dir.empty()) { return dir; }
#ifdef HARMONEYES_THEIA_WEBCAM_SIDECAR_DIR
        return std::filesystem::path{HARMONEYES_THEIA_WEBCAM_SIDECAR_DIR};
#else
        return {};
#endif
    }

    [[nodiscard]] std::string node() const
    {
        if (!config_.node_path.empty()) { return config_.node_path; }
        if (std::string n = env("THEIA_NODE"); !n.empty()) { return n; }
        return "node";
    }

    [[nodiscard]] std::vector<std::string> resolve_command() const
    {
        const std::filesystem::path entry = sidecar_dir() / "sidecar.mjs";
        if (sidecar_dir().empty() || !std::filesystem::exists(entry)) {
            throw std::runtime_error(
                "NexusWebcamTracker: webcam sidecar not found" +
                (sidecar_dir().empty() ? std::string{} : " at " + sidecar_dir().string()) +
                ". Set Config::sidecar_dir (or THEIA_WEBCAM_SIDECAR_DIR) to the "
                "webcam-sidecar directory shipped with this SDK.");
        }
        return {node(), entry.string()};
    }

    /// Where nexus_wasm.wasm lives: the bundled layout first, then a
    /// development checkout's.
    [[nodiscard]] std::filesystem::path resolve_tobii_dir() const
    {
        const std::filesystem::path dir = sidecar_dir();
        if (dir.empty()) { return {}; }
        for (const auto& candidate : {dir / "tobii",
                                      dir / "node_modules" / "tobii_nexus_web" / "dist"}) {
            if (std::filesystem::exists(candidate / "nexus_wasm.wasm")) { return candidate; }
        }
        throw std::runtime_error("NexusWebcamTracker: the Tobii engine is missing from " +
                                 dir.string() + " (expected tobii/nexus_wasm.wasm)");
    }

    [[nodiscard]] std::string resolve_license()
    {
        if (!resolved_license_.empty()) { return resolved_license_; }   // fetched earlier

        std::string url = config_.license_url;
        if (url.empty()) { url = env("TOBII_LICENSE_URL"); }
        if (url.empty()) {
            std::ifstream in{sidecar_dir() / "license-url"};
            std::getline(in, url);
            while (!url.empty() && (url.back() == '\r' || url.back() == ' ')) { url.pop_back(); }
        }
        if (url.empty()) {
            throw std::runtime_error(
                "NexusWebcamTracker: no Tobii licence. Set Config::license_url or "
                "TOBII_LICENSE_URL, or contact support@harmoneyes.com.");
        }
        return fetch_license(url);
    }

    /// Download the licence with Node, which the webcam already needs, so
    /// this SDK does not have to link an HTTP client.
    [[nodiscard]] std::string fetch_license(const std::string& url) const
    {
        static const char* const kFetch =
            "const [u,t]=process.argv.slice(1);"
            "fetch(u,{signal:AbortSignal.timeout(+t)})"
            ".then(async r=>{if(!r.ok){process.stderr.write('HTTP '+r.status);process.exit(2)}"
            "process.stdout.write(Buffer.from(await r.arrayBuffer()))})"
            ".catch(e=>{process.stderr.write(String(e&&e.message||e));process.exit(1)})";

        detail::Process fetcher;
        try {
            fetcher.start({node(), "-e", kFetch, url, "30000"});
        } catch (const std::exception&) {
            throw std::runtime_error(
                "NexusWebcamTracker: Node.js 20+ is required for the webcam. Install it from "
                "nodejs.org, or set THEIA_NODE to its path.");
        }
        fetcher.close_stdin();

        std::vector<unsigned char> body;
        char                       chunk[4096];
        while (const std::size_t got = fetcher.read_stdout(chunk, sizeof(chunk))) {
            body.insert(body.end(), chunk, chunk + got);
        }
        std::string error;
        while (const std::size_t got = fetcher.read_stderr(chunk, sizeof(chunk))) {
            error.append(chunk, got);
        }
        fetcher.wait_for_exit(std::chrono::seconds{5});
        fetcher.terminate();
        fetcher.close_pipes();

        const std::string license = detail::sidecar::decode_utf16le(body);
        if (license.empty()) {
            throw std::runtime_error("NexusWebcamTracker: could not fetch the Tobii licence from " +
                                     url + (error.empty() ? std::string{} : ": " + error));
        }
        return license;
    }

    [[nodiscard]] std::string init_message() const
    {
        namespace sc = detail::sidecar;
        std::string m = R"({"type":"init","tobiiPackageDir":)";
        sc::append_json_string(m, resolved_tobii_dir_.string());
        m += R"(,"frameWidth":)" + std::to_string(config_.width);
        m += R"(,"frameHeight":)" + std::to_string(config_.height);
        m += R"(,"fps":)" + std::to_string(config_.fps);
        m += R"(,"screenWidthMm":)" + std::to_string(config_.screen_width_mm);
        m += R"(,"screenHeightMm":)" + std::to_string(config_.screen_height_mm);
        m += R"(,"fovDeg":)" + sc::format_number(config_.fov_deg);
        m += R"(,"license":)";
        sc::append_json_string(m, resolved_license_);
        if (!config_.device_id.empty()) {
            m += R"(,"deviceId":)";
            sc::append_json_string(m, config_.device_id);
        }
        m += "}";
        return m;
    }

    void handle_line(const std::string& line)
    {
        namespace sc = detail::sidecar;
        const auto type = sc::string_field(line, "type");
        if (!type) { return; }

        if (*type == "gaze") {
            detail::WebcamGaze g;
            g.x     = sc::number_field(line, "x").value_or(0.0);
            g.y     = sc::number_field(line, "y").value_or(0.0);
            g.valid = sc::bool_field(line, "valid").value_or(false);
            // The engine's own capture time, in preference to the sidecar's
            // wall clock, whose event-loop jitter would land in every
            // velocity feature.
            g.timestamp_ms = sc::number_field(line, "timestamp")
                                 .value_or(sc::number_field(line, "wallTime").value_or(0.0));

            detail::WebcamGeometry geometry;
            geometry.screen_width_mm      = config_.screen_width_mm;
            geometry.screen_height_mm     = config_.screen_height_mm;

            std::lock_guard<std::mutex> lock{buffer_mutex_};
            while (buffer_.size() >= kMaxBuffered) { buffer_.pop_front(); }
            buffer_.push_back(detail::build_webcam_sample(g, geometry));
            return;
        }

        if (*type == "ready") {
            { std::lock_guard<std::mutex> lock{ready_mutex_}; ready_ = true; }
            ready_cv_.notify_all();
            return;
        }

        if (*type == "error") {
            {
                std::lock_guard<std::mutex> lock{ready_mutex_};
                failure_ = sc::string_field(line, "msg").value_or("unspecified error");
                // Before readiness an error is fatal; afterwards it is a notice.
                if (!ready_) { failed_ = true; }
            }
            ready_cv_.notify_all();
        }
    }

    void read_loop() noexcept
    {
        std::string pending;
        char        chunk[4096];
        while (running_.load()) {
            const std::size_t got = process_.read_stdout(chunk, sizeof(chunk));
            if (got == 0) { break; }   // end of stream
            pending.append(chunk, got);

            std::size_t start = 0;
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (pending[i] != '\n') { continue; }
                try { handle_line(pending.substr(start, i - start)); } catch (...) {}
                start = i + 1;
            }
            pending.erase(0, start);
            if (pending.size() > 1U << 16U) { pending.clear(); }   // a line that never ends
        }
        stream_ended_ = true;
        {
            std::lock_guard<std::mutex> lock{ready_mutex_};
            if (!ready_) {
                failed_ = true;
                if (failure_.empty()) { failure_ = "the webcam process exited before it was ready"; }
            }
        }
        ready_cv_.notify_all();
    }

    void stderr_loop() noexcept
    {
        std::string pending;
        char        chunk[1024];
        while (running_.load()) {
            const std::size_t got = process_.read_stderr(chunk, sizeof(chunk));
            if (got == 0) { break; }
            pending.append(chunk, got);
            std::size_t start = 0;
            for (std::size_t i = 0; i < pending.size(); ++i) {
                if (pending[i] != '\n') { continue; }
                std::lock_guard<std::mutex> lock{stderr_mutex_};
                if (stderr_lines_.size() >= 40) { stderr_lines_.pop_front(); }
                stderr_lines_.push_back(pending.substr(start, i - start));
                start = i + 1;
            }
            pending.erase(0, start);
            if (pending.size() > 1U << 16U) { pending.clear(); }
        }
    }

    /// The webcam process's recent diagnostics, appended to error messages.
    [[nodiscard]] std::string stderr_tail() const
    {
        std::lock_guard<std::mutex> lock{stderr_mutex_};
        if (stderr_lines_.empty()) { return {}; }
        std::string out = "\n--- webcam process output ---\n";
        for (const auto& line : stderr_lines_) { out += line; out += '\n'; }
        return out;
    }

    /// The shutdown itself. The caller must hold lifecycle_.
    void stop_locked() noexcept
    {
        if (!running_.exchange(false)) { return; }

        if (process_.running()) {
            process_.write_line(R"({"type":"destroy"})");
            process_.close_stdin();
            if (!process_.wait_for_exit(std::chrono::milliseconds{2000})) { process_.terminate(); }
        }
        process_.terminate();   // reap, and guarantee the write ends are gone

        // The child's exit is what gives the readers EOF, so the pipes are
        // closed only after both readers have joined.
        if (reader_.joinable())        { reader_.join(); }
        if (stderr_reader_.joinable()) { stderr_reader_.join(); }
        process_.close_pipes();
        ready_ = false;
    }

    Config                config_;
    detail::Process       process_;
    std::thread           reader_;
    std::thread           stderr_reader_;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     ready_{false};
    std::atomic<bool>     failed_{false};
    std::atomic<bool>     stream_ended_{false};
    std::mutex            lifecycle_;   ///< serialises start() and close()
    std::filesystem::path resolved_tobii_dir_;
    std::string           resolved_license_;

    mutable std::mutex     buffer_mutex_;
    std::deque<GazeSample> buffer_;

    std::mutex              ready_mutex_;
    std::condition_variable ready_cv_;
    std::string             failure_;

    mutable std::mutex      stderr_mutex_;
    std::deque<std::string> stderr_lines_;
};

inline NexusWebcamTracker::NexusWebcamTracker() : NexusWebcamTracker(Config{}) {}

inline NexusWebcamTracker::NexusWebcamTracker(Config config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

inline NexusWebcamTracker::~NexusWebcamTracker() = default;

inline void NexusWebcamTracker::start() { impl_->start(); }

inline std::vector<GazeSample> NexusWebcamTracker::get_buffered_data()
{
    return impl_->get_buffered_data();
}

inline bool NexusWebcamTracker::is_connected() const { return impl_->is_connected(); }

inline void NexusWebcamTracker::close() { impl_->close(); }

}  // namespace harmoneyes::theia
