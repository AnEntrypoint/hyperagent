#pragma once

#include <optional>
#include <string>

#include "journal/records.hpp"

namespace hyperagent::journal {

class Journal {
public:
    Journal(std::string agent, std::string cwd, std::string harness,
            std::optional<std::string> session_id = std::nullopt,
            std::optional<Path> root = std::nullopt);
    ~Journal();

    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    Path bind(const std::optional<std::string>& session_id);
    Value append(const std::string& kind, const std::string& text = "",
                 std::optional<double> epoch = std::nullopt, Value fields = Value::object());
    void flush();
    const Path& path() const;
    std::string session() const;

private:
    Value make_record(const std::string& kind, const std::string& text, Value fields, double epoch) const;
    void write_buffered_records();
    static std::string rebind_text(const std::string& text, const std::string& old_id, const std::string& new_id);

    std::string agent_;
    std::string cwd_;
    std::string harness_;
    std::string fallback_id_;
    std::optional<std::string> session_id_;
    Path root_;
    Path path_;
    std::vector<Value> buffer_;
    double last_write_ = 0.0;
};

}
