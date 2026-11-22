#pragma once

#include <string>

#include "core/platform.hpp"

namespace hyperagent::orch {

class SessionLock {
public:
    SessionLock(const std::string& name, double wait_seconds);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;

private:
    core::platform::LockHandle descriptor_ = core::platform::no_lock;
};

}
