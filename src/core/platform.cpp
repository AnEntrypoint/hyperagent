#include "core/platform.hpp"

#include <cstdlib>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace hyperagent::core::platform {

#if defined(_WIN32)

namespace {

HANDLE handle_of(LockHandle handle)
{
    return reinterpret_cast<HANDLE>(handle);
}

bool lock_region(LockHandle handle, DWORD flags)
{
    OVERLAPPED region{};
    return ::LockFileEx(handle_of(handle), flags, 0, 1, 0, &region) != 0;
}

void append_quoted(std::wstring& out, const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        out += argument;
        return;
    }
    out += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t symbol : argument) {
        if (symbol == L'\\') {
            ++backslashes;
            continue;
        }
        if (symbol == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        backslashes = 0;
        out += symbol;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
}

}

std::wstring wide(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::wstring command_line(const std::vector<std::string>& argv)
{
    std::wstring out;
    for (const auto& argument : argv) {
        if (!out.empty()) {
            out += L' ';
        }
        append_quoted(out, wide(argument));
    }
    return out;
}

LockHandle lock_open(const std::filesystem::path& path)
{
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    return file == INVALID_HANDLE_VALUE ? no_lock : reinterpret_cast<LockHandle>(file);
}

bool lock_try(LockHandle handle)
{
    return lock_region(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY);
}

bool lock_wait(LockHandle handle)
{
    return lock_region(handle, LOCKFILE_EXCLUSIVE_LOCK);
}

void lock_release(LockHandle handle)
{
    OVERLAPPED region{};
    ::UnlockFileEx(handle_of(handle), 0, 1, 0, &region);
    ::CloseHandle(handle_of(handle));
}

bool terminate_process(ProcessId pid)
{
    const HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const bool done = ::TerminateProcess(process, 1) != 0;
    ::CloseHandle(process);
    return done;
}

bool child_exited(ProcessId pid)
{
    const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return true;
    }
    const bool exited = ::WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
    ::CloseHandle(process);
    return exited;
}

bool is_executable(const std::filesystem::path&)
{
    return true;
}

void exec_program(const std::vector<std::string>& argv)
{
    std::wstring line = command_line(argv);
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &child)) {
        std::_Exit(127);
    }
    ::WaitForSingleObject(child.hProcess, INFINITE);
    DWORD code = 127;
    ::GetExitCodeProcess(child.hProcess, &code);
    std::_Exit(static_cast<int>(code));
}

void set_default_environment(const char* name, const char* value)
{
    if (std::getenv(name) == nullptr) {
        ::_putenv_s(name, value);
    }
}

std::size_t terminal_columns()
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        const int columns = info.srWindow.Right - info.srWindow.Left + 1;
        return columns > 0 ? static_cast<std::size_t>(columns) : 0;
    }
    return 0;
}

ProcessId spawn_detached(const std::vector<std::string>& argv, const std::vector<std::string>& environment,
                         const std::filesystem::path& directory, const std::filesystem::path& log)
{
    SECURITY_ATTRIBUTES inheritable{sizeof inheritable, nullptr, TRUE};
    const HANDLE output = ::CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &inheritable, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const HANDLE input = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot open " + log.string());
    }

    std::wstring block;
    for (const auto& entry : environment) {
        block += wide(entry);
        block += L'\0';
    }
    block += L'\0';

    std::wstring line = command_line(argv);
    const std::wstring program = wide(argv.front());
    const std::wstring folder = directory.wstring();
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION child{};
    const BOOL started = ::CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, TRUE,
                                          DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT,
                                          block.data(), folder.c_str(), &startup, &child);
    ::CloseHandle(output);
    ::CloseHandle(input);
    if (!started) {
        throw std::runtime_error("CreateProcessW failed for " + argv.front());
    }
    ::CloseHandle(child.hThread);
    ::CloseHandle(child.hProcess);
    return static_cast<ProcessId>(child.dwProcessId);
}

#else

LockHandle lock_open(const std::filesystem::path& path)
{
    const int descriptor = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    return descriptor < 0 ? no_lock : static_cast<LockHandle>(descriptor);
}

bool lock_try(LockHandle handle)
{
    return ::flock(static_cast<int>(handle), LOCK_EX | LOCK_NB) == 0;
}

bool lock_wait(LockHandle handle)
{
    return ::flock(static_cast<int>(handle), LOCK_EX) == 0;
}

void lock_release(LockHandle handle)
{
    ::flock(static_cast<int>(handle), LOCK_UN);
    ::close(static_cast<int>(handle));
}

bool terminate_process(ProcessId pid)
{
    return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

bool child_exited(ProcessId pid)
{
    int status = 0;
    return ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG) == static_cast<pid_t>(pid);
}

bool is_executable(const std::filesystem::path& path)
{
    return ::access(path.c_str(), X_OK) == 0;
}

void exec_program(const std::vector<std::string>& argv)
{
    std::vector<char*> arguments;
    for (const auto& argument : argv) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);
    ::execvp(arguments[0], arguments.data());
    std::_Exit(127);
}

void set_default_environment(const char* name, const char* value)
{
    ::setenv(name, value, 0);
}

std::size_t terminal_columns()
{
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return 0;
}

#endif

}
