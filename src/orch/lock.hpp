#pragma once

#include <string>

namespace hyperagent::orch {

class SessionLock {
public:
    SessionLock(const std::string& name, double wait_seconds);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;

private:
    int descriptor_ = -1;
};

}
