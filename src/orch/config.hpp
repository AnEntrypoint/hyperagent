#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hyperagent::orch::config {

inline constexpr double default_timeout = 14400.0;
inline constexpr double default_idle_exit = 120.0;
inline constexpr double default_lock_wait = 14400.0;
inline constexpr double default_settle = 3.0;
inline constexpr double prompt_retry_after = 12.0;
inline constexpr int prompt_retries = 2;
inline constexpr double local_command_settle = 5.0;
inline constexpr double poll_interval = 5.0;
inline constexpr std::string_view default_harness = "claude";
inline constexpr std::string_view saved_model_reset = "default";
inline constexpr std::string_view trust_text = "Yes, I trust this folder";
inline constexpr std::string_view trust_hint = "Enter to confirm";

std::string host();
std::string port();
std::string base_url();
std::filesystem::path orch_dir();
std::filesystem::path state_file();
std::filesystem::path state_lock_file();
std::filesystem::path locks_dir();
std::filesystem::path parent_dir();
std::filesystem::path root_dir();
std::string skip_permissions_setting();
std::optional<std::string> claude_model_setting();

bool valid_name(std::string_view name);
bool valid_session_id(std::string_view session_id);
std::string check_name(const std::string& name);
std::string check_session_id(const std::string& session_id);
bool valid_model(std::string_view model);
std::string check_model(const std::string& model);
std::string safe_name(std::string_view raw);
std::string resolve_cwd(const std::optional<std::string>& cwd);
std::string python_repr(std::string_view text);

}
