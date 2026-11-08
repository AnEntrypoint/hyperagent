#include "mcp/turns.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

#include "core/clock.hpp"
#include "mcp/args.hpp"
#include "orch/config.hpp"
#include "orch/turn.hpp"

namespace hyperagent::mcp {

namespace {

constexpr double retention_seconds = 6 * 3600.0;
constexpr double minimum_wait_seconds = 0.1;

struct Record {
    std::string name;
    double started = 0.0;
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Value> result;
};

std::mutex book_mutex;
std::map<std::string, std::shared_ptr<Record>> records;

std::string make_turn_id()
{
    thread_local std::mt19937_64 engine{std::random_device{}()};
    constexpr char digits[] = "0123456789abcdef";
    std::string id;
    for (int index = 0; index < 12; ++index) {
        id.push_back(digits[engine() & 0xF]);
    }
    return id;
}

void prune_locked()
{
    const double cutoff = core::clock::now_epoch() - retention_seconds;
    for (auto iterator = records.begin(); iterator != records.end();) {
        std::lock_guard<std::mutex> lock(iterator->second->mutex);
        const bool finished = iterator->second->result.has_value();
        const bool expired = iterator->second->started < cutoff;
        if (finished && expired) {
            iterator = records.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

Value failure_result(const Value& job, const std::exception& failure, double started)
{
    Value result = Value::object();
    result["name"] = core::json::str_or_empty(core::json::member(&job, "name"));
    const Value* harness = core::json::member(&job, "harness");
    result["harness"] = harness != nullptr ? *harness : Value();
    const Value* cwd = core::json::member(&job, "cwd");
    result["cwd"] = cwd != nullptr ? *cwd : Value();
    result["status"] = "error";
    result["text"] = failure_text(failure);
    result["acked"] = false;
    result["pane_tail"] = Value::array();
    result["duration_s"] = core::clock::round_to(core::clock::now_epoch() - started, 1);
    return result;
}

}

std::string TurnBook::start(const Value& job, const orch::TurnOptions& options)
{
    const std::string turn_id = make_turn_id();
    auto record = std::make_shared<Record>();
    record->name = core::json::str_or_empty(core::json::member(&job, "name"));
    record->started = core::clock::now_epoch();
    {
        std::lock_guard<std::mutex> lock(book_mutex);
        prune_locked();
        records[turn_id] = record;
    }
    std::thread([record, job, options]() {
        Value outcome;
        try {
            outcome = orch::turn::run_job(job, options);
        } catch (const std::exception& failure) {
            outcome = failure_result(job, failure, record->started);
        }
        std::lock_guard<std::mutex> lock(record->mutex);
        record->result = std::move(outcome);
        record->changed.notify_all();
    }).detach();
    return turn_id;
}

Value TurnBook::collect(const std::string& turn_id, double wait_seconds)
{
    std::shared_ptr<Record> record;
    {
        std::lock_guard<std::mutex> lock(book_mutex);
        const auto found = records.find(turn_id);
        if (found == records.end()) {
            throw ToolFailure("ValueError", "unknown turn " + orch::config::python_repr(turn_id));
        }
        record = found->second;
    }
    std::unique_lock<std::mutex> lock(record->mutex);
    if (!record->result.has_value()) {
        const double bounded = std::max(minimum_wait_seconds, wait_seconds);
        record->changed.wait_for(lock, std::chrono::duration<double>(bounded),
                                 [&record]() { return record->result.has_value(); });
    }
    Value out = Value::object();
    out["turn_id"] = turn_id;
    out["name"] = record->name;
    if (!record->result.has_value()) {
        out["state"] = "running";
        out["elapsed_s"] = core::clock::round_to(core::clock::now_epoch() - record->started, 1);
        return out;
    }
    out["state"] = "done";
    out["result"] = *record->result;
    return out;
}

TurnBook& turn_book()
{
    static TurnBook book;
    return book;
}

}
