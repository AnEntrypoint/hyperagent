#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace hyperagent::core::platform {

#if defined(_WIN32)
inline constexpr char path_separator = ';';
inline constexpr const char* executable_suffix = ".exe";
#else
inline constexpr char path_separator = ':';
inline constexpr const char* executable_suffix = "";
#endif

using LockHandle = std::intptr_t;
inline constexpr LockHandle no_lock = -1;

LockHandle lock_open(const std::filesystem::path& path);
bool lock_try(LockHandle handle);
bool lock_wait(LockHandle handle);
void lock_release(LockHandle handle);

using ProcessId = long long;

bool terminate_process(ProcessId pid);
bool child_exited(ProcessId pid);
bool is_executable(const std::filesystem::path& path);
[[noreturn]] void exec_program(const std::vector<std::string>& argv);
void set_default_environment(const char* name, const char* value);
std::size_t terminal_columns();

#if defined(_WIN32)
std::wstring wide(const std::string& text);
std::wstring command_line(const std::vector<std::string>& argv);
ProcessId spawn_detached(const std::vector<std::string>& argv, const std::vector<std::string>& environment,
                         const std::filesystem::path& directory, const std::filesystem::path& log);
#endif

inline std::tm utc_time(std::time_t moment)
{
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &moment);
#else
    gmtime_r(&moment, &out);
#endif
    return out;
}

inline std::tm local_time(std::time_t moment)
{
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &moment);
#else
    localtime_r(&moment, &out);
#endif
    return out;
}

inline std::time_t utc_epoch(std::tm& moment)
{
#if defined(_WIN32)
    return _mkgmtime(&moment);
#else
    return timegm(&moment);
#endif
}

}
