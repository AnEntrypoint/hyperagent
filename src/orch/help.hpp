#pragma once

#include <memory>

#include <CLI/FormatterFwd.hpp>

namespace hyperagent::orch::help {

std::shared_ptr<CLI::FormatterBase> make_formatter();

}
