#include "orch/lock.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

#include "orch/config.hpp"
#include "orch/orch.hpp"

namespace hyperagent::orch {

SessionLock::SessionLock(const std::string& name, double wait_seconds)
{
    const auto directory = config::locks_dir();
    std::filesystem::create_directories(directory);
    descriptor_ = core::platform::lock_open(directory / (name + ".lock"));
    if (descriptor_ == core::platform::no_lock) {
        throw Error("cannot open lock for agent " + config::python_repr(name));
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(wait_seconds));
    while (!core::platform::lock_try(descriptor_)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            core::platform::lock_release(descriptor_);
            descriptor_ = core::platform::no_lock;
            throw Error("agent " + config::python_repr(name) + " is already being prompted");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

SessionLock::~SessionLock()
{
    if (descriptor_ != core::platform::no_lock) {
        core::platform::lock_release(descriptor_);
    }
}

}
