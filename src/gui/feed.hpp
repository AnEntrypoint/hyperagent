#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "gui/hub.hpp"
#include "gui/tail.hpp"

namespace hyperagent::gui {

class JournalFeed {
public:
    JournalFeed(std::filesystem::path root, std::filesystem::path server_log);

    void run();
    std::string backfill();
    Hub& hub();

private:
    void discover(bool initial);

    std::filesystem::path root_;
    std::filesystem::path server_log_;
    Hub hub_;
    std::mutex mutex_;
    std::vector<std::unique_ptr<Tail>> tails_;
};

}
