#include "journal/command.hpp"

#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "journal/journal.hpp"
#include "journal/records.hpp"

namespace hyperagent::journal {

namespace {

std::optional<std::string> maybe(const std::string& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::optional<Path> maybe_path(const std::string& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    return Path(value);
}

void print_json(const Value& value)
{
    std::cout << core::json::dumps(value, core::json::spaced) << "\n";
}

struct RootedQuery {
    std::string root;
    std::string cwd;
    std::string agent;
};

void add_rooted_options(CLI::App* command, RootedQuery& query)
{
    command->add_option("--root", query.root, "journal root (default ORCH_JOURNAL or .orch/projects)");
    command->add_option("--cwd", query.cwd, "working directory whose journal to read")->required();
    command->add_option("--agent", query.agent, "peer name")->required();
}

void add_scan(CLI::App& journal)
{
    auto query = std::make_shared<RootedQuery>();
    auto session = std::make_shared<std::string>();
    auto limit = std::make_shared<std::size_t>(0);
    auto* command = journal.add_subcommand("scan", "list session journals as JSON, newest first");
    command->add_option("--root", query->root, "journal root (default ORCH_JOURNAL or .orch/projects)");
    command->add_option("--cwd", query->cwd, "restrict to one working directory");
    command->add_option("--agent", query->agent, "restrict to one peer name");
    command->add_option("--session", *session, "restrict to one session id");
    command->add_option("--limit", *limit, "keep at most this many rows");
    command->callback([query, session, limit]() {
        print_json(Value(scan(maybe_path(query->root), maybe(query->cwd), maybe(query->agent),
                              maybe(*session), *limit)));
    });
}

void add_output(CLI::App& journal)
{
    auto query = std::make_shared<RootedQuery>();
    auto* command = journal.add_subcommand("output", "latest output of a peer as [summary, record]");
    add_rooted_options(command, *query);
    command->callback([query]() {
        const auto output = latest_output(query->cwd, query->agent, maybe_path(query->root));
        Value pair = Value::array();
        pair.push_back(output.summary ? *output.summary : Value(nullptr));
        pair.push_back(output.record ? *output.record : Value(nullptr));
        print_json(pair);
    });
}

void add_response(CLI::App& journal)
{
    auto query = std::make_shared<RootedQuery>();
    auto* command = journal.add_subcommand("response", "last response block of a peer as [path, record]");
    add_rooted_options(command, *query);
    command->callback([query]() {
        const auto located = last_response(query->cwd, query->agent, maybe_path(query->root));
        Value pair = Value::array();
        pair.push_back(located ? Value(located->path.string()) : Value(nullptr));
        pair.push_back(located ? located->record : Value(nullptr));
        print_json(pair);
    });
}

void add_result(CLI::App& journal)
{
    auto query = std::make_shared<RootedQuery>();
    auto* command = journal.add_subcommand("result", "latest result record of a peer as [path, record]");
    add_rooted_options(command, *query);
    command->callback([query]() {
        const auto located = latest_result(query->cwd, query->agent, maybe_path(query->root));
        Value pair = Value::array();
        pair.push_back(located ? Value(located->path.string()) : Value(nullptr));
        pair.push_back(located ? located->record : Value(nullptr));
        print_json(pair);
    });
}

void add_records(CLI::App& journal)
{
    auto file = std::make_shared<std::string>();
    auto kinds = std::make_shared<std::vector<std::string>>();
    auto limit = std::make_shared<std::size_t>(0);
    auto since = std::make_shared<double>(0.0);
    auto* command = journal.add_subcommand("records", "every record of a journal file as a JSON array");
    command->add_option("file", *file, "journal .jsonl file")->required();
    command->add_option("--kind", *kinds, "keep only this kind (repeatable)");
    command->add_option("--limit", *limit, "keep only the last N records");
    command->add_option("--since", *since, "keep records with epoch at or after this value");
    command->callback([file, kinds, limit, since, command]() {
        std::optional<double> cutoff;
        if (command->count("--since") > 0) {
            cutoff = *since;
        }
        std::optional<std::size_t> keep;
        if (*limit > 0) {
            keep = *limit;
        }
        Value records = Value::array();
        for (const auto& record : read_records(*file, *kinds, keep, cutoff)) {
            records.push_back(record);
        }
        print_json(records);
    });
}

void add_tail(CLI::App& journal)
{
    auto file = std::make_shared<std::string>();
    auto kinds = std::make_shared<std::vector<std::string>>();
    auto limit = std::make_shared<long>(40);
    auto max_bytes = std::make_shared<std::uintmax_t>(1000000);
    auto* command = journal.add_subcommand("tail", "last records of a journal file as a JSON array");
    command->add_option("file", *file, "journal .jsonl file")->required();
    command->add_option("--kind", *kinds, "keep only this kind (repeatable)");
    command->add_option("--limit", *limit, "records to keep");
    command->add_option("--max-bytes", *max_bytes, "bytes read from the end of the file");
    command->callback([file, kinds, limit, max_bytes]() {
        Value records = Value::array();
        for (const auto& record : tail_records(*file, *limit, *kinds, *max_bytes)) {
            records.push_back(record);
        }
        print_json(records);
    });
}

void add_render(CLI::App& journal)
{
    auto file = std::make_shared<std::string>();
    auto kinds = std::make_shared<std::vector<std::string>>();
    auto limit = std::make_shared<long>(40);
    auto header = std::make_shared<std::string>();
    auto max_chars = std::make_shared<long>(20000);
    auto per_record = std::make_shared<long>(2000);
    auto* command = journal.add_subcommand("render", "render the last records of a journal file as text");
    command->add_option("file", *file, "journal .jsonl file")->required();
    command->add_option("--kind", *kinds, "keep only this kind (repeatable)");
    command->add_option("--limit", *limit, "records to keep");
    command->add_option("--header", *header, "line printed before the records");
    command->add_option("--max-chars", *max_chars, "cap on the whole rendering");
    command->add_option("--per-record", *per_record, "cap on each record body");
    command->callback([file, kinds, limit, header, max_chars, per_record]() {
        const auto records = tail_records(*file, *limit, *kinds);
        std::cout << render_records(records, *header, *max_chars, *per_record) << "\n";
    });
}

void apply_operations(Journal& journal, const Value& operations)
{
    for (const auto& operation : operations) {
        const std::string name = operation.value("op", std::string());
        if (name == "append") {
            std::optional<double> epoch;
            if (operation.contains("epoch") && !operation["epoch"].is_null()) {
                epoch = operation["epoch"].get<double>();
            }
            Value fields = operation.contains("fields") ? operation["fields"] : Value::object();
            journal.append(operation.value("kind", std::string()), operation.value("text", std::string()),
                           epoch, fields);
        } else if (name == "bind") {
            std::optional<std::string> session;
            if (operation.contains("session") && operation["session"].is_string()) {
                session = operation["session"].get<std::string>();
            }
            journal.bind(session);
        }
    }
}

void add_write(CLI::App& journal)
{
    auto root = std::make_shared<std::string>();
    auto cwd = std::make_shared<std::string>();
    auto agent = std::make_shared<std::string>();
    auto harness = std::make_shared<std::string>();
    auto session = std::make_shared<std::string>();
    auto operations = std::make_shared<std::string>();
    auto* command = journal.add_subcommand("write", "apply a JSON list of append and bind operations");
    command->add_option("--root", *root, "journal root");
    command->add_option("--cwd", *cwd, "working directory")->required();
    command->add_option("--agent", *agent, "peer name")->required();
    command->add_option("--harness", *harness, "harness name")->required();
    command->add_option("--session", *session, "initial session id");
    command->add_option("--ops", *operations, "JSON list of operations")->required();
    command->callback([=]() {
        const auto parsed = core::json::parse(*operations);
        if (!parsed || !parsed->is_array()) {
            throw CLI::ValidationError("--ops", "expected a JSON array");
        }
        Journal writer(*agent, *cwd, *harness, maybe(*session), maybe_path(*root));
        apply_operations(writer, *parsed);
        writer.flush();
        std::cout << writer.path().string() << "\n";
    });
}

}

void add_command(CLI::App& app)
{
    auto* journal = app.add_subcommand("journal", "per-session output journal under .orch/projects");
    journal->require_subcommand(1);
    add_scan(*journal);
    add_output(*journal);
    add_response(*journal);
    add_result(*journal);
    add_records(*journal);
    add_tail(*journal);
    add_render(*journal);
    add_write(*journal);
}

}
