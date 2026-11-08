#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "gui/hub.hpp"

namespace hyperagent::gui {

class ReloadWatcher {
public:
    explicit ReloadWatcher(std::filesystem::path public_dir);

    void run();
    Hub& hub();

private:
    using Stamps = std::map<std::string, std::filesystem::file_time_type>;

    Stamps snapshot() const;
    void publish(const std::vector<std::string>& changed);

    std::filesystem::path public_dir_;
    Hub hub_;
};

}
