#include "gate/gate.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/process.hpp"
#include "core/text.hpp"
#include "gate/host.hpp"

namespace hyperagent::gate {

namespace {

using core::json::member;
using Value = core::json::Value;

constexpr double learn_margin = 1.15;
constexpr double learn_min_second_mb = 3000;
constexpr double swap_out_limit_kbs = 1000;
constexpr double takes_turns = 2;
constexpr std::size_t learned_window = 3;

std::optional<std::string> env_text(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::string env_or(const char* name, const std::string& fallback)
{
    return env_text(name).value_or(fallback);
}

bool env_is(const char* name, const char* expected)
{
    const auto text = env_text(name);
    return text.has_value() && *text == expected;
}

double env_number(const char* name, double fallback)
{
    const auto text = env_text(name);
    return text ? std::strtod(text->c_str(), nullptr) : fallback;
}

double number_text(const std::string& text)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' ? value : std::numeric_limits<double>::quiet_NaN();
}

std::filesystem::path path_env(const char* name, const std::filesystem::path& fallback)
{
    const auto text = env_text(name);
    return text ? std::filesystem::path(*text) : fallback;
}

std::filesystem::path scratch_root()
{
    if (const auto raw = env_text("GATE_SCRATCH")) {
        return core::paths::expand_user(*raw);
    }
    std::filesystem::path base = core::paths::repo_root().parent_path() / "tmp";
    if (const auto raw = env_text("HYPERAGENT_SCRATCH")) {
        base = core::paths::expand_user(*raw);
    }
    return base / "gate";
}

struct Settings {
    std::filesystem::path ticket_dir;
    std::filesystem::path legacy_ticket_dir;
    std::filesystem::path history;
    std::filesystem::path last_turns;
    std::filesystem::path results;
    std::string reading_command;
    double poll_ms = 10000;
    long long peak_sample_ms = 5000;
    double short_run_s = 300;
    double slots = 2;
    double first_slot_mb = 2500;
    double second_slot_mb = 6000;
    double quick_slot_mb = 4000;
    double quick_slots = 1;
};

Settings settings()
{
    const auto scratch = scratch_root();
    Settings s;
    s.ticket_dir = path_env("GATE_TICKET_DIR", scratch / "tickets");
    s.legacy_ticket_dir = std::filesystem::path(env_or("TMPDIR", "/tmp")) / "gate-tickets";
    s.history = path_env("GATE_HISTORY", scratch / "history.jsonl");
    s.last_turns = s.ticket_dir / "turns.json";
    s.results = scratch / "results";
    s.reading_command = env_or("GATE_READING_CMD", "");
    s.poll_ms = env_number("GATE_POLL_MS", s.poll_ms);
    s.peak_sample_ms = static_cast<long long>(env_number("GATE_PEAK_SAMPLE_MS", static_cast<double>(s.peak_sample_ms)));
    s.short_run_s = env_number("GATE_SHORT_RUN_S", s.short_run_s);
    s.slots = env_number("GATE_SLOTS", s.slots);
    s.first_slot_mb = env_number("GATE_FIRST_SLOT_MB", s.first_slot_mb);
    s.second_slot_mb = env_number("GATE_SECOND_SLOT_MB", s.second_slot_mb);
    s.quick_slot_mb = env_number("GATE_QUICK_SLOT_MB", s.quick_slot_mb);
    s.quick_slots = env_number("GATE_QUICK_SLOTS", s.quick_slots);
    return s;
}

double number_of(const Value* object, std::string_view key)
{
    const Value* found = member(object, key);
    return found != nullptr && found->is_number() ? found->get<double>() : 0.0;
}

std::string text_of(const Value* object, std::string_view key)
{
    const Value* found = member(object, key);
    return found != nullptr && found->is_string() ? found->get<std::string>() : std::string();
}

bool succeeded(const Value& row)
{
    const Value* code = member(&row, "code");
    return code != nullptr && code->is_number() && code->get<double>() == 0.0;
}

Value whole(double number)
{
    if (number == std::floor(number) && std::fabs(number) < 9e15) {
        return Value(static_cast<long long>(number));
    }
    return Value(number);
}

double js_round(double value)
{
    return std::floor(value + 0.5);
}

long long now_ms()
{
    return static_cast<long long>(std::floor(core::clock::now_epoch() * 1000.0));
}

std::string owner_of(const std::string& label)
{
    return label.substr(0, label.find('-'));
}

bool digits_only(std::string_view text)
{
    return !text.empty()
        && std::all_of(text.begin(), text.end(), [](unsigned char symbol) { return std::isdigit(symbol) != 0; });
}

bool ticket_name(const std::string& name, long long& pid)
{
    constexpr std::string_view suffix = ".json";
    constexpr std::size_t arrival_digits = 15;
    if (name.size() < arrival_digits + 2 + suffix.size() || name[arrival_digits] != '-'
        || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return false;
    }
    const std::string number = name.substr(arrival_digits + 1, name.size() - arrival_digits - 1 - suffix.size());
    if (!digits_only(name.substr(0, arrival_digits)) || !digits_only(number)) {
        return false;
    }
    pid = std::stoll(number);
    return true;
}

struct Row {
    std::string name;
    Value data = Value::object();
    int rank = 0;
    double owner_turn = 0;
};

std::string label_of(const Row& row)
{
    return text_of(&row.data, "label");
}

bool flag(const Row& row, std::string_view key)
{
    return core::json::truthy(member(&row.data, key));
}

bool granted(const Row& row)
{
    return flag(row, "granted");
}

bool quick(const Row& row)
{
    return flag(row, "quick");
}

bool exclusive(const Row& row)
{
    return flag(row, "exclusive");
}

double field(const Row& row, std::string_view key)
{
    return number_of(&row.data, key);
}

bool takes_turns_ticket(const Row& row)
{
    const Value* version = member(&row.data, "v");
    return version != nullptr && version->is_number() && version->get<double>() == takes_turns;
}

const Row* find_row(const std::vector<Row>& rows, const std::string& name)
{
    for (const auto& row : rows) {
        if (row.name == name) {
            return &row;
        }
    }
    return nullptr;
}

Value turns(const Settings& s)
{
    const auto parsed = core::json::read_file(s.last_turns);
    return parsed && parsed->is_object() ? *parsed : Value::object();
}

void write_json(const std::filesystem::path& file, const Value& value)
{
    core::files::ensure_parent_directory(file);
    core::files::write_bytes(file, core::json::dumps(value) + "\n");
}

std::vector<Row> live_tickets(const Settings& s)
{
    std::error_code ignored;
    std::filesystem::create_directories(s.ticket_dir, ignored);
    std::vector<std::filesystem::path> dirs{s.ticket_dir};
    if (std::filesystem::exists(s.legacy_ticket_dir, ignored) && s.legacy_ticket_dir != s.ticket_dir) {
        dirs.push_back(s.legacy_ticket_dir);
    }

    std::vector<Row> live;
    for (const auto& dir : dirs) {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ignored)) {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            long long pid = 0;
            if (!ticket_name(name, pid)) {
                continue;
            }
            const auto file = dir / name;
            if (!host::alive(pid)) {
                std::filesystem::remove(file, ignored);
                continue;
            }
            Row row;
            row.name = name;
            const auto parsed = core::json::read_file(file);
            if (parsed && parsed->is_object()) {
                row.data = *parsed;
            }
            live.push_back(std::move(row));
        }
    }
    return live;
}

std::vector<Row> in_turn(const Settings& s, std::vector<Row> rows)
{
    if (!std::all_of(rows.begin(), rows.end(), takes_turns_ticket)) {
        return rows;
    }
    const Value last = turns(s);
    std::set<std::string> queued;
    for (auto& row : rows) {
        const std::string owner = owner_of(label_of(row));
        row.rank = granted(row) ? 0 : queued.count(owner) != 0 ? 2 : 1;
        row.owner_turn = row.rank == 1 ? number_of(&last, owner) : 0;
        if (!granted(row)) {
            queued.insert(owner);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
        if (left.rank != right.rank) {
            return left.rank < right.rank;
        }
        if (left.owner_turn != right.owner_turn) {
            return left.owner_turn < right.owner_turn;
        }
        return left.name < right.name;
    });
    return rows;
}

std::vector<Value> history_rows(const Settings& s)
{
    std::vector<Value> rows;
    const auto bytes = core::files::read_bytes(s.history);
    if (!bytes) {
        return rows;
    }
    std::size_t start = 0;
    while (start <= bytes->size()) {
        const auto end = bytes->find('\n', start);
        const std::string line = bytes->substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? bytes->size() + 1 : end + 1;
        if (line.empty()) {
            continue;
        }
        const auto row = core::json::parse(line);
        if (!row) {
            throw std::runtime_error("gate history has an unreadable row");
        }
        rows.push_back(*row);
    }
    return rows;
}

double learned_mb(const std::vector<Value>& rows, const std::string& label)
{
    std::vector<double> peaks;
    for (const auto& row : rows) {
        if (text_of(&row, "label") == label && number_of(&row, "peakMb") > 0) {
            peaks.push_back(number_of(&row, "peakMb"));
        }
    }
    if (peaks.empty()) {
        return 0;
    }
    const std::size_t count = std::min(learned_window, peaks.size());
    return std::ceil(*std::max_element(peaks.end() - static_cast<std::ptrdiff_t>(count), peaks.end()) * learn_margin);
}

double last_ran(const std::vector<Value>& rows, const std::string& label)
{
    double ran = 0;
    for (const auto& row : rows) {
        if (text_of(&row, "label") == label && succeeded(row)) {
            ran = number_of(&row, "ran");
        }
    }
    return ran;
}

double available_mb(const Settings& s)
{
    if (const auto override_mb = env_text("GATE_MEMORY_MB")) {
        return std::strtod(override_mb->c_str(), nullptr);
    }
    if (!s.reading_command.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream fields(line.substr(std::string("MemAvailable:").size()));
            double kb = 0;
            fields >> kb;
            return std::floor(kb / 1024.0);
        }
    }
    throw std::runtime_error("MemAvailable is missing from /proc/meminfo");
}

std::vector<std::string> words_of(std::string_view text)
{
    std::vector<std::string> words;
    std::string current;
    for (const char symbol : text) {
        if (std::isspace(static_cast<unsigned char>(symbol)) != 0) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(symbol);
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

std::string last_line_of(const std::string& text)
{
    const std::string trimmed(core::text::strip(text));
    const auto cut = trimmed.rfind('\n');
    return cut == std::string::npos ? trimmed : trimmed.substr(cut + 1);
}

bool host_room(const Settings& s, double floor_mb)
{
    if (!s.reading_command.empty()) {
        const auto probe = core::process::run_capture({"bash", "-c", s.reading_command});
        return probe.exit_code == 0 && available_mb(s) >= floor_mb;
    }
    const auto sample = core::process::run_capture({"vmstat", "1", "2"});
    const auto columns = words_of(last_line_of(sample.standard_output));
    const double swap_out = columns.size() > 7 ? number_text(columns[7]) : std::numeric_limits<double>::quiet_NaN();
    return swap_out < swap_out_limit_kbs && available_mb(s) >= floor_mb;
}

bool my_turn(const Settings& s, const std::string& name)
{
    const auto rows = in_turn(s, live_tickets(s));
    const Row* mine = find_row(rows, name);
    if (mine != nullptr && quick(*mine)) {
        const auto busy = std::count_if(rows.begin(), rows.end(),
                                        [](const Row& row) { return quick(row) && granted(row); });
        return static_cast<double>(busy) < s.quick_slots && host_room(s, s.quick_slot_mb);
    }
    if (!std::all_of(rows.begin(), rows.end(), takes_turns_ticket)) {
        const double own_floor = mine != nullptr ? field(*mine, "minMb") : 0.0;
        return !rows.empty() && rows.front().name == name && host_room(s, std::max(s.first_slot_mb, own_floor));
    }

    std::vector<const Row*> running;
    const Row* next = nullptr;
    for (const auto& row : rows) {
        if (granted(row) && !quick(row)) {
            running.push_back(&row);
        }
        if (next == nullptr && !granted(row) && !quick(row)) {
            next = &row;
        }
    }
    if (static_cast<double>(running.size()) >= s.slots) {
        return false;
    }
    const bool running_exclusive = std::any_of(running.begin(), running.end(),
                                               [](const Row* row) { return exclusive(*row); });
    if (next == nullptr || next->name != name) {
        const bool passes_blocked_head = next != nullptr && exclusive(*next) && !running.empty() && !running_exclusive
            && mine != nullptr && !exclusive(*mine) && field(*mine, "lastRan") > 0
            && field(*mine, "lastRan") <= s.short_run_s;
        return passes_blocked_head
            && host_room(s, std::max({learn_min_second_mb, field(*mine, "learnedMb"), field(*mine, "minMb")}));
    }
    const double learned = field(*next, "learnedMb");
    if (running.empty()) {
        return host_room(s, std::max({learned, s.first_slot_mb, field(*next, "minMb")}));
    }
    const double second = learned != 0 ? std::max(learn_min_second_mb, learned) : s.second_slot_mb;
    return !exclusive(*next) && !running_exclusive && host_room(s, std::max(second, field(*next, "minMb")));
}

void write_result(const Settings& s, long long pid, Value result)
{
    result["ticket"] = pid;
    write_json(s.results / (std::to_string(pid) + ".json"), result);
}

struct TicketFile {
    std::filesystem::path path;

    ~TicketFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

std::string safe_component(const std::string& text)
{
    std::string out;
    for (const char symbol : text) {
        const bool keep = std::isalnum(static_cast<unsigned char>(symbol)) != 0 || symbol == '.' || symbol == '_'
            || symbol == '-';
        out.push_back(keep ? symbol : '_');
    }
    return out;
}

}

int run(const std::string& label, const std::vector<std::string>& command)
{
    if (label.empty() || command.empty()) {
        throw std::runtime_error("usage: gate run <label> -- <command> [args]");
    }
    const Settings s = settings();
    const std::string tree = env_or("GATE_TREE", "");
    const auto history = history_rows(s);
    const long long pid = host::current_pid();

    if (!tree.empty() && !env_text("GATE_FORCE")
        && std::any_of(history.begin(), history.end(), [&](const Value& row) {
               return text_of(&row, "label") == label && text_of(&row, "tree") == tree && succeeded(row);
           })) {
        std::cout << "CACHED " << label << " on " << tree << " already passed; GATE_FORCE=1 reruns it" << std::endl;
        Value cached = Value::object();
        cached["label"] = label;
        cached["tree"] = tree;
        cached["cached"] = true;
        cached["code"] = 0;
        write_result(s, pid, cached);
        return 0;
    }

    long long latest = 0;
    for (const auto& row : live_tickets(s)) {
        latest = std::max(latest, std::stoll(row.name.substr(0, 15)));
    }
    const long long arrival = std::max(now_ms(), latest + 1);
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%015lld-%lld.json", arrival, pid);
    const std::string name = stamp;
    const auto file = s.ticket_dir / name;

    Value ticket = Value::object();
    ticket["pid"] = pid;
    ticket["arrival"] = arrival;
    ticket["label"] = label;
    ticket["v"] = static_cast<int>(takes_turns);
    ticket["exclusive"] = env_is("GATE_EXCLUSIVE", "1");
    ticket["quick"] = env_is("GATE_QUICK", "1");
    ticket["minMb"] = whole(env_number("GATE_MIN_MB", 0));
    ticket["learnedMb"] = whole(learned_mb(history, label));
    ticket["lastRan"] = whole(last_ran(history, label));
    ticket["tree"] = tree;
    write_json(file, ticket);

    const TicketFile guard{file};
    host::stop_on_signal(file);
    const auto poll = std::chrono::milliseconds(static_cast<long long>(s.poll_ms));
    while (!my_turn(s, name)) {
        std::this_thread::sleep_for(poll);
    }

    const long long granted_at = now_ms();
    Value granted_ticket = ticket;
    granted_ticket["granted"] = granted_at;
    write_json(file, granted_ticket);
    Value last = turns(s);
    last[owner_of(label)] = granted_at;
    write_json(s.last_turns, last);
    std::cout << "GRANTED " << label << " " << name.substr(0, 15) << std::endl;

    long long peak_kb = 0;
    const int code = host::run_sampled(command, s.peak_sample_ms, peak_kb);
    const long long ended_at = now_ms();

    Value row = Value::object();
    row["label"] = label;
    row["tree"] = tree;
    row["waited"] = whole(js_round(static_cast<double>(granted_at - arrival) / 1000.0));
    row["ran"] = whole(js_round(static_cast<double>(ended_at - granted_at) / 1000.0));
    row["peakMb"] = whole(js_round(static_cast<double>(peak_kb) / 1024.0));
    row["code"] = code;
    row["ended"] = core::clock::iso_stamp(static_cast<double>(ended_at) / 1000.0);
    core::files::ensure_parent_directory(s.history);
    core::files::append_bytes(s.history, core::json::dumps(row) + "\n");
    write_result(s, pid, row);
    return code;
}

Started start(const Request& request)
{
    if (request.label.empty() || request.command.empty()) {
        throw std::runtime_error("gate run needs a label and a command");
    }
    const Settings s = settings();
    std::error_code ignored;
    std::filesystem::create_directories(s.results, ignored);

    std::vector<std::string> argv{host::executable().string(), "gate", "run", request.label, "--"};
    argv.insert(argv.end(), request.command.begin(), request.command.end());
    const std::map<std::string, std::string> overrides{
        {"GATE_TREE", request.tree},
        {"GATE_EXCLUSIVE", request.exclusive ? "1" : ""},
        {"GATE_QUICK", request.quick ? "1" : ""},
        {"GATE_MIN_MB", request.min_mb},
        {"GATE_FORCE", request.force ? "1" : ""},
    };
    const auto log = s.results / (safe_component(request.label) + "-" + std::to_string(now_ms()) + ".log");
    const long long pid = host::spawn_detached(argv, overrides, log);

    Started started;
    started.ticket = pid;
    started.result = s.results / (std::to_string(pid) + ".json");
    started.log = log;
    return started;
}

std::vector<std::string> list_lines()
{
    const Settings s = settings();
    std::vector<std::string> names;
    for (const auto& row : in_turn(s, live_tickets(s))) {
        names.push_back(row.name);
    }
    return names;
}

std::vector<std::string> history_lines()
{
    struct Totals {
        long long windows = 0;
        long long waited = 0;
        long long ran = 0;
        long long longest = 0;
        std::string longest_label;
    };

    const Settings s = settings();
    std::vector<std::pair<std::string, Totals>> owners;
    for (const auto& row : history_rows(s)) {
        const std::string owner = owner_of(text_of(&row, "label"));
        auto found = std::find_if(owners.begin(), owners.end(), [&](const auto& entry) { return entry.first == owner; });
        if (found == owners.end()) {
            owners.emplace_back(owner, Totals{});
            found = std::prev(owners.end());
        }
        Totals& total = found->second;
        const auto ran = static_cast<long long>(number_of(&row, "ran"));
        total.windows += 1;
        total.waited += static_cast<long long>(number_of(&row, "waited"));
        total.ran += ran;
        if (ran > total.longest) {
            total.longest = ran;
            total.longest_label = text_of(&row, "label");
        }
    }

    std::vector<std::string> lines;
    for (const auto& [owner, total] : owners) {
        lines.push_back(owner + " windows=" + std::to_string(total.windows) + " waited=" + std::to_string(total.waited)
                        + "s ran=" + std::to_string(total.ran) + "s longest=" + std::to_string(total.longest) + "s "
                        + total.longest_label);
    }
    return lines;
}

}
