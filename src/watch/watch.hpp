#pragma once

#include <string>

#include "monitor/command.hpp"
#include "monitor/monitor.hpp"

namespace hyperagent::watch {

class CcbridgeSource : public monitor::SessionSource {
public:
    CcbridgeSource(std::string host, std::string port);

    monitor::Value sessions() override;
    std::string capture(const std::string& name) override;

private:
    std::string host_;
    std::string port_;
};

int run(const monitor::CommandOptions& options);

}
