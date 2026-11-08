#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace hyperagent::core::paths {

std::filesystem::path repo_root();
std::filesystem::path expand_user(std::string_view text);
std::string absolute_path(std::string_view text);
std::filesystem::path resolve(const std::filesystem::path& path);
std::string slug(std::string_view cwd);
std::filesystem::path orch_home();
std::filesystem::path journal_root();
std::filesystem::path project_dir(std::string_view cwd,
                                  const std::optional<std::filesystem::path>& root = std::nullopt);
std::filesystem::path session_path(std::string_view cwd, std::string_view session_id,
                                   const std::optional<std::filesystem::path>& root = std::nullopt);

}
