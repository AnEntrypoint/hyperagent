#include "orch/cycle.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/clock.hpp"
#include "core/files.hpp"
#include "core/http.hpp"
#include "core/json.hpp"
#include "core/paths.hpp"
#include "core/platform.hpp"
#include "core/process.hpp"
#include "core/text.hpp"
#include "gate/gate.hpp"
#include "journal/records.hpp"

namespace hyperagent::orch::cycle {

namespace {

using core::json::member;
using Value = core::json::Value;

constexpr std::uintmax_t tail_bytes = 400000;
constexpr std::size_t clip_record_text = 260;
constexpr std::size_t clip_ticket_args = 110;
constexpr std::size_t clip_sent_text = 200;
constexpr std::size_t clip_orphan_args = 90;
constexpr std::size_t sent_tail = 4;

struct Peer {
    std::string name;
    std::string session;
    std::string checkout;
    std::string remote;
};

struct Ticket {
    std::string pid;
    std::string args;
    std::string child;
    std::string owner;
};

std::optional<std::string> env_text(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::vector<std::string> split_on(std::string_view text, char separator)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const auto found = text.find(separator, start);
        parts.emplace_back(text.substr(start, found == std::string_view::npos ? std::string_view::npos : found - start));
        if (found == std::string_view::npos) {
            break;
        }
        start = found + 1;
    }
    return parts;
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

std::string join_with(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string out;
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            out += separator;
        }
        out += parts[index];
    }
    return out;
}

std::string one_line(std::string_view text)
{
    std::string out;
    bool gap = false;
    for (const char symbol : text) {
        if (std::isspace(static_cast<unsigned char>(symbol)) != 0) {
            gap = !out.empty();
            continue;
        }
        if (gap) {
            out.push_back(' ');
            gap = false;
        }
        out.push_back(symbol);
    }
    return out;
}

std::string clipped(std::string_view text, std::size_t limit)
{
    return std::string(core::text::code_point_prefix(text, limit));
}

std::string or_unknown(const std::string& text)
{
    return text.empty() ? std::string("?") : text;
}

double js_round(double value)
{
    return std::floor(value + 0.5);
}

double number_text(const std::string& text)
{
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' ? value : std::numeric_limits<double>::quiet_NaN();
}

double now_ms()
{
    return std::floor(core::clock::now_epoch() * 1000.0);
}

std::string text_field(const Value* record, std::string_view key)
{
    const Value* found = member(record, key);
    return found != nullptr && found->is_string() ? found->get<std::string>() : std::string();
}

double number_field(const Value* record, std::string_view key)
{
    const Value* found = member(record, key);
    return found != nullptr && found->is_number() ? found->get<double>() : 0.0;
}

bool is_kind(const Value* record, std::string_view kind)
{
    return text_field(record, "kind") == kind;
}

std::string display(const Value* record, std::string_view key)
{
    const Value* found = member(record, key);
    if (found == nullptr) {
        return "undefined";
    }
    if (found->is_string()) {
        return found->get<std::string>();
    }
    return core::json::dumps(*found);
}

std::optional<double> parse_time(const std::string& text)
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return std::nullopt;
    }

    constexpr std::size_t seconds_end = 19;
    std::size_t cursor = seconds_end;
    double milliseconds = 0;
    if (text.size() > cursor && text[cursor] == '.') {
        ++cursor;
        double fraction = 0;
        double scale = 1;
        while (cursor < text.size() && std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            fraction = fraction * 10 + (text[cursor] - '0');
            scale *= 10;
            ++cursor;
        }
        milliseconds = fraction / scale * 1000.0;
    }

    double offset_minutes = 0;
    if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) {
        int offset_hours = 0;
        int offset_rest = 0;
        if (std::sscanf(text.c_str() + cursor + 1, "%2d:%2d", &offset_hours, &offset_rest) == 2) {
            offset_minutes = (offset_hours * 60 + offset_rest) * (text[cursor] == '+' ? 1 : -1);
        }
    }

    std::tm moment{};
    moment.tm_year = year - 1900;
    moment.tm_mon = month - 1;
    moment.tm_mday = day;
    moment.tm_hour = hour;
    moment.tm_min = minute;
    moment.tm_sec = second;
    const auto seconds = core::platform::utc_epoch(moment);
    return static_cast<double>(seconds) * 1000.0 + milliseconds - offset_minutes * 60000.0;
}

double time_of(const Value* record)
{
    if (const auto parsed = parse_time(text_field(record, "ts"))) {
        return *parsed;
    }
    const double epoch = number_field(record, "epoch");
    return epoch > 1e12 ? epoch : epoch * 1000;
}

std::string age_of(const Value* record)
{
    const std::string ts = text_field(record, "ts");
    if (ts.empty()) {
        return "?";
    }
    const auto parsed = parse_time(ts);
    if (!parsed) {
        return "NaNs";
    }
    return std::to_string(static_cast<long long>(js_round((now_ms() - *parsed) / 1000.0))) + "s";
}

std::string capture(const std::vector<std::string>& argv)
{
    const auto result = core::process::run_capture(argv);
    return result.exit_code == 0 ? std::string(core::text::strip(result.standard_output)) : std::string();
}

std::string first_line(const std::string& text)
{
    return split_on(text, '\n').front();
}

std::string last_line(const std::string& text)
{
    return split_on(text, '\n').back();
}

std::string git(const std::filesystem::path& cwd, const std::vector<std::string>& arguments)
{
    std::vector<std::string> argv{"git", "-C", cwd.string()};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return capture(argv);
}

std::string remote_head(const std::string& remote, const std::filesystem::path& cwd)
{
    if (remote == "gh") {
        git(cwd, {"-c", "credential.helper=", "-c", "credential.helper=!gh auth git-credential", "fetch", "origin",
                  "--quiet"});
        return git(cwd, {"rev-parse", "--short", "origin/main"});
    }
    if (remote == "fetch") {
        git(cwd, {"fetch", "origin", "--quiet"});
        return git(cwd, {"rev-parse", "--short", "origin/main"});
    }
    if (remote == "ls-remote") {
        return git(cwd, {"ls-remote", "origin", "main"}).substr(0, 7);
    }
    throw std::runtime_error("unknown remote " + remote);
}

std::string dirty_count(const std::filesystem::path& cwd)
{
    const auto result = core::process::run_capture({"git", "-C", cwd.string(), "status", "--short"});
    return std::to_string(std::count(result.standard_output.begin(), result.standard_output.end(), '\n'));
}

std::vector<std::string> peer_roots_for_checkouts()
{
    std::vector<std::string> roots;
    if (const auto raw = env_text("HYPERAGENT_PEERS_ROOTS")) {
        for (const auto& part : split_on(*raw, core::platform::path_separator)) {
            if (!part.empty()) {
                roots.push_back(part);
            }
        }
    }
    roots.push_back(core::paths::repo_root().parent_path().string());
    if (const auto scratch = env_text("HYPERAGENT_SCRATCH")) {
        roots.push_back(std::filesystem::path(*scratch).parent_path().string());
    }
    return roots;
}

std::filesystem::path checkout_of(const Peer& peer)
{
    const std::filesystem::path checkout(peer.checkout);
    if (checkout.is_absolute()) {
        return checkout;
    }
    const auto roots = peer_roots_for_checkouts();
    std::error_code ignored;
    for (const auto& base : roots) {
        const auto candidate = std::filesystem::absolute(std::filesystem::path(base) / checkout).lexically_normal();
        if (std::filesystem::exists(candidate, ignored)) {
            return candidate;
        }
    }
    return std::filesystem::absolute(std::filesystem::path(roots.front()) / checkout).lexically_normal();
}

std::filesystem::path journal_file(const Peer& peer, const std::string& file)
{
    std::optional<std::filesystem::path> override_root;
    if (const auto raw = env_text("HYPERAGENT_JOURNAL")) {
        override_root = std::filesystem::path(*raw);
    }
    return core::paths::project_dir(checkout_of(peer).string(), override_root) / file;
}

std::vector<Value> records_for(const Peer& peer)
{
    std::vector<std::pair<double, Value>> timed;
    const std::vector<std::filesystem::path> files{journal_file(peer, peer.session + ".jsonl"),
                                                  journal_file(peer, "peer-" + peer.name + ".jsonl")};
    for (const auto& file : files) {
        for (auto& record : journal::read_records(file)) {
            timed.emplace_back(time_of(&record), std::move(record));
        }
    }
    std::stable_sort(timed.begin(), timed.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<Value> records;
    records.reserve(timed.size());
    for (auto& entry : timed) {
        records.push_back(std::move(entry.second));
    }
    return records;
}

std::vector<Value> newest_records(const Peer& peer)
{
    auto bound = journal::tail_records(journal_file(peer, peer.session + ".jsonl"),
                                       std::numeric_limits<long>::max(), {}, tail_bytes);
    auto unbound = journal::tail_records(journal_file(peer, "peer-" + peer.name + ".jsonl"),
                                         std::numeric_limits<long>::max(), {}, tail_bytes);
    const double bound_time = time_of(bound.empty() ? nullptr : &bound.back());
    const double unbound_time = time_of(unbound.empty() ? nullptr : &unbound.back());
    return unbound_time > bound_time ? unbound : bound;
}

std::vector<Peer> load_peers()
{
    const auto document = core::json::read_file(core::paths::repo_root() / "scripts" / "peers.json");
    if (!document || !document->is_array()) {
        throw std::runtime_error("scripts/peers.json is missing or unreadable");
    }
    std::vector<Peer> peers;
    for (const auto& item : *document) {
        Peer peer;
        peer.name = text_field(&item, "name");
        peer.session = text_field(&item, "session");
        peer.checkout = text_field(&item, "checkout");
        peer.remote = text_field(&item, "remote");
        peers.push_back(peer);
    }
    return peers;
}

Value live_sessions()
{
    const std::string host = env_text("ORCH_HOST").value_or("127.0.0.1");
    const std::string port = env_text("ORCH_PORT").value_or("8788");
    try {
        const auto response = core::http::get(host, port, "/sessions", std::chrono::seconds(2));
        const auto parsed = core::json::parse(response.body);
        return parsed ? *parsed : Value::object();
    } catch (const std::exception&) {
        return Value::object();
    }
}

std::vector<std::string> queue_items(const std::string& name)
{
    std::vector<std::string> items;
    const auto text = core::files::read_bytes(core::paths::repo_root() / "specs" / "queue" / (name + ".md"));
    if (!text) {
        return items;
    }
    for (const auto& line : split_on(*text, '\n')) {
        std::size_t digits = 0;
        while (digits < line.size() && std::isdigit(static_cast<unsigned char>(line[digits])) != 0) {
            ++digits;
        }
        if (digits > 0 && digits + 1 < line.size() && line[digits] == '.' && line[digits + 1] == ' ') {
            items.push_back(line);
        }
    }
    return items;
}

std::string pid_of(const std::string& name)
{
    const auto dash = name.rfind('-');
    const auto dot = name.rfind(".json");
    if (dash == std::string::npos || dot == std::string::npos || dot < dash) {
        return name;
    }
    return name.substr(dash + 1, dot - dash - 1);
}

std::string owner_in(const std::string& args)
{
    const std::string marker = " run ";
    for (auto at = args.find(marker); at != std::string::npos; at = args.find(marker, at + 1)) {
        const std::size_t begin = at + marker.size();
        std::size_t end = begin;
        while (end < args.size() && std::isspace(static_cast<unsigned char>(args[end])) == 0 && args[end] != '-') {
            ++end;
        }
        if (end > begin) {
            return args.substr(begin, end - begin);
        }
    }
    return std::string();
}

std::vector<Ticket> ticket_views()
{
    std::vector<Ticket> tickets;
    for (const auto& name : gate::list_lines()) {
        Ticket ticket;
        ticket.pid = pid_of(name);
        ticket.args = clipped(one_line(capture({"ps", "-o", "etimes=,args=", "-p", ticket.pid})), clip_ticket_args);
        ticket.child = first_line(capture({"pgrep", "-P", ticket.pid}));
        ticket.owner = owner_in(ticket.args);
        tickets.push_back(ticket);
    }
    return tickets;
}

std::string peer_line(const Peer& peer, const std::vector<Ticket>& tickets, const Value& sessions)
{
    const auto cwd = checkout_of(peer);
    std::error_code ignored;
    const bool exists = std::filesystem::exists(cwd, ignored);
    const auto records = newest_records(peer);
    const Value* last = records.empty() ? nullptr : &records.back();
    const Value* last_text = nullptr;
    for (auto record = records.rbegin(); record != records.rend(); ++record) {
        if (is_kind(&*record, "text") && !text_field(&*record, "text").empty()) {
            last_text = &*record;
            break;
        }
    }
    const bool ended = is_kind(last, "result");

    const std::string head = exists ? git(cwd, {"rev-parse", "--short", "HEAD"}) : std::string();
    const std::string origin = exists ? remote_head(peer.remote, cwd) : std::string();
    std::string ahead;
    if (exists) {
        if (peer.remote == "ls-remote") {
            ahead = head == origin ? "0" : "?";
        } else {
            ahead = git(cwd, {"rev-list", "--count", "origin/main..HEAD"});
        }
    }
    const std::string dirty = exists ? dirty_count(cwd) : std::string();

    const std::size_t queued = queue_items(peer.name).size();
    const auto owned = static_cast<std::size_t>(std::count_if(
        tickets.begin(), tickets.end(), [&](const Ticket& ticket) { return ticket.owner == peer.name; }));
    const Value* live = member(&sessions, peer.name);
    const bool is_live = core::json::truthy(live);
    const bool due = owned == 0 && queued > 0;

    std::string state;
    if (!exists) {
        state = "MISSING";
    } else if (!is_live) {
        state = ended ? (due ? "ENDED DUE" : "ENDED") : "STOPPED";
    } else if (core::json::truthy(member(live, "busy"))) {
        state = "running";
    } else {
        state = ended ? (due ? "ENDED DUE" : "ENDED") : "IDLE";
    }

    const std::vector<std::string> fields{
        peer.name,
        state,
        "last=" + age_of(last),
        "HEAD=" + head,
        "origin=" + origin,
        "ahead=" + ahead,
        "dirty=" + dirty,
        "queue=" + std::to_string(queued),
        "tickets=" + std::to_string(owned),
        std::string("live=") + (is_live ? "1" : "0"),
        "| " + clipped(one_line(text_field(last_text, "text")), clip_record_text),
    };
    return join_with(fields, " ");
}

bool is_system_process(const std::string& args)
{
    static const std::vector<std::string> markers{
        "s6", "dbus", "gvfs", "xfce", "Xvfb", "ssh-agent", "gpg-agent", "at-spi", "dconf", "udisks", "crashpad",
        "tumbler", "gate-ticket.cjs", "ccbridge serve", "hyperagent orch p", "hyperagent gate run",
    };
    return std::any_of(markers.begin(), markers.end(),
                       [&](const std::string& marker) { return args.find(marker) != std::string::npos; });
}

std::string utc_clock()
{
    const std::time_t now = std::time(nullptr);
    const std::tm utc = core::platform::utc_time(now);
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d", utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buffer;
}

std::string disk_free(const std::string& target)
{
    return or_unknown(last_line(capture({"df", "--output=avail", "-BG", target})));
}

std::vector<std::string> host_lines()
{
    const auto vm = words_of(last_line(capture({"vmstat", "1", "2"})));
    const auto column = [&](std::size_t index) -> std::string {
        return index < vm.size() ? vm[index] : std::string();
    };
    const std::string swap_in = or_unknown(column(6));
    const std::string swap_out = or_unknown(column(7));
    const std::string idle = or_unknown(column(14));
    const double swap = vm.size() > 2 ? number_text(vm[2]) : 0.0;
    const std::string swap_text = std::isfinite(swap)
        ? std::to_string(static_cast<long long>(js_round(swap / 1024.0))) + "MB"
        : std::string("?");

    std::string available;
    const auto free_lines = split_on(capture({"free", "-m"}), '\n');
    if (free_lines.size() >= 2) {
        const auto fields = words_of(free_lines[1]);
        if (fields.size() >= 7) {
            available = fields[6];
        }
    }

    const std::string network = capture({"curl", "-s", "-o", "/dev/null", "-m", "6", "-w", "%{http_code}",
                                         "https://api.anthropic.com/"});
    const auto root = core::paths::repo_root();
    std::vector<std::string> lines{
        "host " + utc_clock() + " si=" + swap_in + " so=" + swap_out + " KB/s idle=" + idle + "% available="
            + or_unknown(available) + "MB swap=" + swap_text + " root=" + disk_free(root.root_path().string())
            + " workspace=" + disk_free(root.parent_path().string()) + " api="
            + (network.empty() ? std::string("unreachable") : network),
    };

    const auto process_lines = split_on(capture({"ps", "-eo", "pid,ppid,etimes,rss,args"}), '\n');
    for (std::size_t index = 1; index < process_lines.size(); ++index) {
        const auto fields = words_of(process_lines[index]);
        if (fields.size() < 4 || fields[1] != "1") {
            continue;
        }
        std::vector<std::string> args(fields.begin() + 4, fields.end());
        const std::string command = join_with(args, " ");
        if (is_system_process(command)) {
            continue;
        }
        const auto megabytes = static_cast<long long>(js_round(number_text(fields[3]) / 1024.0));
        lines.push_back("orphan " + fields[0] + " " + fields[2] + "s " + std::to_string(megabytes) + "MB "
                        + clipped(command, clip_orphan_args));
    }
    return lines;
}

std::vector<std::string> spec_names()
{
    std::vector<std::string> names;
    const auto dir = core::paths::repo_root() / "specs";
    std::error_code ignored;
    if (!std::filesystem::exists(dir, ignored)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ignored)) {
        const std::string name = entry.path().filename().string();
        if (name.size() >= 3 && name.compare(name.size() - 3, 3, ".md") == 0) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void emit(const std::function<std::string()>& produce)
{
    try {
        std::cout << produce() << std::flush;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << "\n";
        std::exit(1);
    }
}

}

std::vector<std::string> board()
{
    const auto peers = load_peers();
    const auto tickets = ticket_views();
    const Value sessions = live_sessions();

    std::vector<std::string> lines;
    for (const auto& peer : peers) {
        lines.push_back(peer_line(peer, tickets, sessions));
    }
    for (const auto& ticket : tickets) {
        const std::string state = ticket.child.empty() ? std::string("queued") : "RUNNING child=" + ticket.child;
        lines.push_back("ticket " + ticket.pid + " " + state + " " + ticket.args);
    }
    for (const auto& name : spec_names()) {
        lines.push_back("spec " + name);
    }
    for (const auto& line : host_lines()) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> queue(const std::string& peer)
{
    return queue_items(peer);
}

std::vector<std::string> sent(const std::string& name, const std::string& phrase)
{
    const auto peers = load_peers();
    const auto found = std::find_if(peers.begin(), peers.end(), [&](const Peer& peer) { return peer.name == name; });
    if (found == peers.end()) {
        throw std::runtime_error("unknown peer " + name);
    }
    const auto records = records_for(*found);
    const std::string wanted = one_line(phrase);

    std::optional<std::size_t> index;
    for (std::size_t position = records.size(); position-- > 0;) {
        if (one_line(text_field(&records[position], "text")).find(wanted) != std::string::npos) {
            index = position;
            break;
        }
    }
    if (!index) {
        return {"NOT DELIVERED " + name + ": the phrase is not in the journal"};
    }

    std::vector<const Value*> after;
    for (std::size_t position = *index + 1; position < records.size(); ++position) {
        const std::string kind = text_field(&records[position], "kind");
        if (kind == "text" || kind == "tool" || kind == "result") {
            after.push_back(&records[position]);
        }
    }

    std::vector<std::string> lines;
    lines.push_back((after.empty() ? std::string("IN JOURNAL, NO RECORD AFTER IT YET ") : std::string("DELIVERED "))
                    + name + " at " + display(&records[*index], "ts") + ", " + std::to_string(after.size())
                    + " records after");
    const std::size_t shown = std::min(sent_tail, after.size());
    for (std::size_t position = after.size() - shown; position < after.size(); ++position) {
        lines.push_back(display(after[position], "ts") + " " + text_field(after[position], "kind") + " "
                        + clipped(one_line(text_field(after[position], "text")), clip_sent_text));
    }
    return lines;
}

std::vector<std::string> specs()
{
    std::vector<std::string> lines;
    for (const auto& name : spec_names()) {
        lines.push_back("spec " + name);
    }
    return lines;
}

void add_command(CLI::App& app)
{
    app.add_subcommand("cycle", "status board: peer state, git sync, queue, tickets, specs and host")
        ->callback([]() { emit([]() { return core::text::join_lines(board()); }); });

    auto peer = std::make_shared<std::string>();
    auto* queue_command = app.add_subcommand("queue", "queued items of one peer from specs/queue");
    queue_command->add_option("peer", *peer)->required();
    queue_command->callback([peer]() { emit([peer]() { return core::text::join_lines(queue(*peer)); }); });

    auto arguments = std::make_shared<std::vector<std::string>>();
    auto* sent_command = app.add_subcommand("sent", "find a phrase in a peer's journal: delivered or not, and what followed");
    sent_command->add_option("arguments", *arguments, "peer followed by the phrase")->required();
    sent_command->callback([arguments]() {
        if (arguments->size() < 2) {
            std::cerr << "usage: sent <peer> <phrase>\n";
            std::exit(2);
        }
        const std::string name = arguments->front();
        const std::string phrase = join_with(std::vector<std::string>(arguments->begin() + 1, arguments->end()), " ");
        emit([name, phrase]() { return core::text::join_lines(sent(name, phrase)); });
    });
}

}
