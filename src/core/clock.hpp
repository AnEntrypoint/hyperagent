#pragma once

#include <string>

namespace hyperagent::core::clock {

double now_epoch();
double round_to(double value, int digits);
std::string iso_stamp(double epoch);
std::string http_date();

}
