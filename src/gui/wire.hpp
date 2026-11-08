#pragma once

#include <string>
#include <vector>

#include "core/json.hpp"

namespace hyperagent::gui::wire {

using Value = core::json::Value;

std::string dumps_ascii(const Value& value);
Value array_of(const std::vector<Value>& items);
double epoch_of(const Value& record);
void sort_by_epoch(std::vector<Value>& records);

}
