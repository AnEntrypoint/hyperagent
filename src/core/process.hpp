#pragma once

#include <string>
#include <vector>

namespace hyperagent::core::process {

struct Result {
    int exit_code;
    std::string standard_output;
    std::string standard_error;
};

Result run_capture(const std::vector<std::string>& argv);

}
