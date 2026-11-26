#include "mcp/ops.hpp"

#include <string>
#include <vector>

#include "core/text.hpp"
#include "gate/gate.hpp"
#include "mcp/args.hpp"
#include "orch/cycle.hpp"

namespace hyperagent::mcp::ops {

std::string cycle(const Value&)
{
    return core::text::join_lines(orch::cycle::board());
}

std::string queue(const Value& args)
{
    return core::text::join_lines(orch::cycle::queue(text_arg(args, "peer")));
}

std::string sent(const Value& args)
{
    return core::text::join_lines(orch::cycle::sent(text_arg(args, "peer"), text_arg(args, "phrase")));
}

std::string specs(const Value&)
{
    return core::text::join_lines(orch::cycle::specs());
}

std::string gate_run(const Value& args)
{
    gate::Request request;
    request.label = text_arg(args, "label");
    const Value* command = arg(args, "command");
    if (command != nullptr && command->is_array()) {
        request.command = text_list(command);
    }
    if (request.label.empty() || request.command.empty()) {
        throw ToolFailure("ValueError", "label and command are required");
    }
    request.tree = text_arg(args, "tree");
    request.exclusive = flag_arg(args, "exclusive", false);
    request.quick = flag_arg(args, "quick", false);
    request.force = flag_arg(args, "force", false);
    const double min_mb = number_arg(args, "min_mb", 0);
    if (min_mb != 0) {
        request.min_mb = std::to_string(min_mb);
    }

    const gate::Started started = gate::start(request);
    Value out = Value::object();
    out["ticket"] = started.ticket;
    out["result"] = started.result.string();
    out["log"] = started.log.string();
    return dumps_indented(out);
}

std::string gate_list(const Value&)
{
    return core::text::join_lines(gate::list_lines());
}

std::string gate_history(const Value&)
{
    return core::text::join_lines(gate::history_lines());
}

}
