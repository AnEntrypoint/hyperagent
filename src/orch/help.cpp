#include "orch/help.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <CLI/CLI.hpp>

namespace hyperagent::orch::help {

namespace {

constexpr std::size_t max_help_position = 24;
constexpr std::size_t fallback_columns = 80;
constexpr std::size_t minimum_help_width = 11;

enum class Nargs { one, one_or_more, optional, zero_or_more };

struct Arg {
    std::string flag;
    std::string alias;
    std::string metavar;
    Nargs nargs = Nargs::one;
    std::string help;

    bool positional() const
    {
        return flag.empty();
    }

    bool takes_value() const
    {
        return !metavar.empty();
    }
};

Arg help_flag()
{
    return Arg{"-h", "--help", "", Nargs::one, "show this help message and exit"};
}

Arg flag(std::string name, std::string help = "")
{
    return Arg{std::move(name), "", "", Nargs::one, std::move(help)};
}

Arg option(std::string name, std::string metavar, std::string help = "", Nargs nargs = Nargs::one)
{
    return Arg{std::move(name), "", std::move(metavar), nargs, std::move(help)};
}

Arg positional(std::string metavar, Nargs nargs, std::string help = "")
{
    return Arg{"", "", std::move(metavar), nargs, std::move(help)};
}

std::string join(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string out;
    bool first = true;
    for (const auto& part : parts) {
        out += first ? part : separator + part;
        first = false;
    }
    return out;
}

std::size_t terminal_columns()
{
    if (const char* env = std::getenv("COLUMNS"); env != nullptr) {
        const long value = std::strtol(env, nullptr, 10);
        if (value > 0) {
            return static_cast<std::size_t>(value);
        }
    }
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return size.ws_col;
    }
    return fallback_columns;
}

std::string value_text(const Arg& arg)
{
    if (arg.nargs == Nargs::one_or_more) {
        return arg.metavar + " [" + arg.metavar + " ...]";
    }
    return arg.metavar;
}

std::string positional_usage(const Arg& arg)
{
    if (arg.nargs == Nargs::optional) {
        return "[" + arg.metavar + "]";
    }
    if (arg.nargs == Nargs::zero_or_more) {
        return "[" + arg.metavar + " ...]";
    }
    return value_text(arg);
}

std::string usage_part(const Arg& arg)
{
    if (arg.positional()) {
        return positional_usage(arg);
    }
    if (arg.takes_value()) {
        return "[" + arg.flag + " " + value_text(arg) + "]";
    }
    return "[" + arg.flag + "]";
}

std::string invocation(const Arg& arg)
{
    if (arg.positional()) {
        return arg.metavar;
    }
    if (!arg.alias.empty()) {
        return arg.flag + ", " + arg.alias;
    }
    if (arg.takes_value()) {
        return arg.flag + " " + value_text(arg);
    }
    return arg.flag;
}

std::vector<std::string> wrap_text(const std::string& text, std::size_t width)
{
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string word;
    std::string line;
    while (words >> word) {
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1 + word.size() <= width) {
            line += " " + word;
        } else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> wrap_parts(const std::vector<std::string>& parts, const std::string& indent,
                                    std::size_t width, bool with_prefix)
{
    std::vector<std::string> lines;
    std::vector<std::string> line;
    std::size_t line_len = (with_prefix ? std::string("usage: ").size() : indent.size()) - 1;
    for (const auto& part : parts) {
        if (line_len + 1 + part.size() > width && !line.empty()) {
            lines.push_back(indent + join(line, " "));
            line.clear();
            line_len = indent.size() - 1;
        }
        line.push_back(part);
        line_len += part.size() + 1;
    }
    if (!line.empty()) {
        lines.push_back(indent + join(line, " "));
    }
    if (with_prefix && !lines.empty()) {
        lines.front() = lines.front().substr(indent.size());
    }
    return lines;
}

std::string usage_text(const std::string& prog, const std::vector<Arg>& options,
                       const std::vector<Arg>& positionals, std::size_t width)
{
    const std::string prefix = "usage: ";
    std::vector<std::string> option_parts;
    for (const auto& arg : options) {
        option_parts.push_back(usage_part(arg));
    }
    std::vector<std::string> positional_parts;
    for (const auto& arg : positionals) {
        positional_parts.push_back(usage_part(arg));
    }

    std::vector<std::string> whole{prog};
    whole.insert(whole.end(), option_parts.begin(), option_parts.end());
    whole.insert(whole.end(), positional_parts.begin(), positional_parts.end());
    const std::string single_line = join(whole, " ");
    if (prefix.size() + single_line.size() <= width) {
        return prefix + single_line;
    }

    const std::string indent(prefix.size() + prog.size() + 1, ' ');
    std::vector<std::string> head{prog};
    head.insert(head.end(), option_parts.begin(), option_parts.end());
    std::vector<std::string> lines = wrap_parts(head, indent, width, true);
    const std::vector<std::string> tail = wrap_parts(positional_parts, indent, width, false);
    lines.insert(lines.end(), tail.begin(), tail.end());
    return prefix + join(lines, "\n");
}

std::string render_items(const std::vector<Arg>& args, std::size_t help_position, std::size_t width)
{
    const std::size_t help_width = std::max(width - help_position, minimum_help_width);
    const std::size_t action_width = help_position - 4;
    const std::string pad(help_position, ' ');
    std::string out;
    for (const auto& arg : args) {
        const std::string header = invocation(arg);
        if (arg.help.empty()) {
            out += "  " + header + "\n";
            continue;
        }
        const std::vector<std::string> lines = wrap_text(arg.help, help_width);
        if (header.size() <= action_width) {
            out += "  " + header + std::string(action_width - header.size() + 2, ' ') + lines.front() + "\n";
        } else {
            out += "  " + header + "\n" + pad + lines.front() + "\n";
        }
        for (std::size_t index = 1; index < lines.size(); ++index) {
            out += pad + lines[index] + "\n";
        }
    }
    return out;
}

std::string render(const std::string& prog, const std::vector<Arg>& args)
{
    const std::size_t width = terminal_columns() - 2;
    std::vector<Arg> options{help_flag()};
    std::vector<Arg> positionals;
    for (const auto& arg : args) {
        (arg.positional() ? positionals : options).push_back(arg);
    }

    std::size_t action_max = 0;
    for (const auto* group : {&options, &positionals}) {
        for (const auto& arg : *group) {
            action_max = std::max(action_max, invocation(arg).size() + 2);
        }
    }
    const std::size_t help_position = std::min(action_max + 2, max_help_position);

    std::string out = usage_text(prog, options, positionals, width) + "\n";
    if (!positionals.empty()) {
        out += "\npositional arguments:\n" + render_items(positionals, help_position, width);
    }
    out += "\noptions:\n" + render_items(options, help_position, width);
    return out;
}

std::optional<std::vector<Arg>> spec_for(const std::string& name)
{
    if (name == "up" || name == "down") {
        return std::vector<Arg>{};
    }
    if (name == "status") {
        return std::vector<Arg>{flag("--json"), flag("--panes")};
    }
    if (name == "projects") {
        return std::vector<Arg>{flag("--all", "include directories without a project marker"), flag("--json")};
    }
    if (name == "start") {
        return std::vector<Arg>{
            positional("NAME", Nargs::zero_or_more),
            option("--cwd", "CWD"),
            option("--harness", "HARNESS", "harness for the peer; defaults to the project's remembered harness"),
            option("--model", "MODEL", "model for the peer; saved with the project and reused on every launch, \"default\" forgets it"),
            option("--settle", "SETTLE"),
            option("--concurrency", "CONCURRENCY"),
            flag("--resume", "resume each project's latest session when it is not running"),
            flag("--fork", "fork the resumed session instead of reusing its id"),
            flag("--no-trust"),
        };
    }
    if (name == "prompt") {
        return std::vector<Arg>{
            positional("name", Nargs::one),
            positional("text", Nargs::one_or_more),
            option("--cwd", "CWD"),
            option("--harness", "HARNESS", "harness for the peer; defaults to the project's remembered harness"),
            option("--model", "MODEL", "model for the peer; saved with the project and reused on every launch, \"default\" forgets it"),
            option("--timeout", "TIMEOUT"),
            option("--idle-exit", "IDLE_EXIT"),
            option("--lock-wait", "LOCK_WAIT"),
            flag("--tools"),
            flag("--json"),
            flag("--resume", "resume the project's latest session when it is not running"),
            option("--session", "SESSION", "resume this harness session id before prompting"),
            flag("--fork", "fork the resumed session instead of reusing its id"),
            flag("--no-trust"),
        };
    }
    if (name == "fanout") {
        return std::vector<Arg>{
            positional("file", Nargs::one),
            option("--timeout", "TIMEOUT"),
            option("--idle-exit", "IDLE_EXIT"),
            option("--lock-wait", "LOCK_WAIT"),
            flag("--tools"),
            flag("--json"),
            flag("--no-trust"),
        };
    }
    if (name == "resume") {
        return std::vector<Arg>{
            positional("name", Nargs::optional, "project name; omitted resumes the last project"),
            option("--session", "SESSION", "specific harness session id"),
            option("--cwd", "CWD", "project directory when the name is not discovered"),
            flag("--continue", "use the harness's continue flag instead of a session id"),
            flag("--fork", "fork the session instead of reusing its id"),
            option("--prompt", "PROMPT", "prompt to send after resuming", Nargs::one_or_more),
            option("--harness", "HARNESS", "harness to resume; defaults to the project's remembered harness"),
            option("--model", "MODEL", "model for the peer; saved with the project and reused on every launch, \"default\" forgets it"),
            option("--timeout", "TIMEOUT"),
            option("--idle-exit", "IDLE_EXIT"),
            option("--lock-wait", "LOCK_WAIT"),
            option("--settle", "SETTLE"),
            flag("--tools"),
            flag("--json"),
            flag("--no-trust"),
        };
    }
    if (name == "sessions") {
        return std::vector<Arg>{
            positional("name", Nargs::optional, "project name; omitted uses the last project"),
            option("--cwd", "CWD", "project directory when the name is not discovered"),
            option("--harness", "HARNESS", "harness to list; defaults to the project's remembered harness"),
            flag("--all", "list sessions for every discovered project and harness"),
            flag("--json"),
        };
    }
    if (name == "stop") {
        return std::vector<Arg>{positional("name", Nargs::one)};
    }
    if (name == "pane") {
        return std::vector<Arg>{positional("name", Nargs::one), option("--lines", "LINES")};
    }
    if (name == "outputs") {
        return std::vector<Arg>{
            positional("name", Nargs::optional, "project name; omitted uses the last project"),
            flag("--all"),
            option("--limit", "LIMIT"),
            flag("--json"),
        };
    }
    if (name == "output") {
        return std::vector<Arg>{
            positional("name", Nargs::optional, "project name; omitted searches every project"),
            option("--agent", "AGENT"),
            option("--session", "SESSION"),
            option("--tail", "TAIL"),
            option("--kind", "KIND"),
            flag("--json"),
        };
    }
    return std::nullopt;
}

class ArgparseHelp final : public CLI::FormatterBase {
public:
    std::string make_help(const CLI::App* app, std::string name, CLI::AppFormatMode mode) const override
    {
        const auto spec = spec_for(app->get_name());
        if (mode != CLI::AppFormatMode::Normal || app->get_parent() == nullptr || !spec) {
            return fallback_.make_help(app, std::move(name), mode);
        }
        return render(app->get_parent()->get_name() + " " + app->get_name(), *spec);
    }

private:
    CLI::Formatter fallback_;
};

}

std::shared_ptr<CLI::FormatterBase> make_formatter()
{
    return std::make_shared<ArgparseHelp>();
}

}
