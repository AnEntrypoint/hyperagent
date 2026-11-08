#include "orch/lock.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

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
    descriptor_ = ::open((directory / (name + ".lock")).c_str(), O_CREAT | O_RDWR, 0600);
    if (descriptor_ < 0) {
        throw Error("cannot open lock for agent " + config::python_repr(name));
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(wait_seconds));
    while (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
        if (std::chrono::steady_clock::now() >= deadline) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw Error("agent " + config::python_repr(name) + " is already being prompted");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

SessionLock::~SessionLock()
{
    if (descriptor_ >= 0) {
        ::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
}

}
