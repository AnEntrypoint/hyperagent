#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace hyperagent::gate::host {

long long current_pid();
bool alive(long long pid);
std::filesystem::path executable();
long long spawn_detached(const std::vector<std::string>& argv, const std::map<std::string, std::string>& overrides,
                         const std::filesystem::path& log);
int run_sampled(const std::vector<std::string>& argv, long long sample_ms, long long& peak_kb);
long long tree_rss_kb(long long root);
void stop_on_signal(const std::filesystem::path& cleanup);

}
