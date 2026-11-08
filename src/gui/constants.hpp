#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace hyperagent::gui::constants {

inline constexpr auto poll_interval = std::chrono::milliseconds(200);
inline constexpr auto discover_interval = std::chrono::seconds(1);
inline constexpr auto watch_interval = std::chrono::milliseconds(300);
inline constexpr auto keepalive_interval = std::chrono::seconds(15);
inline constexpr std::size_t read_block = 65536;
inline constexpr std::size_t backfill_per_source = 500;
inline constexpr std::size_t backfill_total = 1500;
inline constexpr std::size_t subscriber_depth = 4096;
inline constexpr std::uint16_t default_port = 8790;
inline constexpr const char* default_host = "127.0.0.1";

}
