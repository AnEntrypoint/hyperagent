#include "mcp/tools.hpp"

#include <map>
#include <set>

#include "mcp/ops.hpp"
#include "mcp/outputs.hpp"
#include "mcp/peers.hpp"
#include "mcp/schema.hpp"

namespace hyperagent::mcp::tools {

namespace {

using Handler = std::string (*)(const Value&);

const std::map<std::string, Handler>& handlers()
{
    static const std::map<std::string, Handler> table{
        {"list_projects", peers::list_projects},
        {"start_projects", peers::start_projects},
        {"instruct", peers::instruct},
        {"instruct_many", peers::instruct_many},
        {"turn_result", peers::turn_result},
        {"resume_project", peers::resume_project},
        {"list_project_sessions", peers::list_project_sessions},
        {"peer_status", peers::peer_status},
        {"read_pane", peers::read_pane},
        {"stop_peer", peers::stop_peer},
        {"server_down", peers::server_down},
        {"list_outputs", outputs::list_outputs},
        {"read_output", outputs::read_output},
        {"search_output", outputs::search_output},
        {"cycle", ops::cycle},
        {"queue", ops::queue},
        {"sent", ops::sent},
        {"specs", ops::specs},
        {"gate_run", ops::gate_run},
        {"gate_list", ops::gate_list},
        {"gate_history", ops::gate_history},
    };
    return table;
}

}

Value descriptors()
{
    static const Value tools = core::json::parse(schema::tools_json).value();
    return tools;
}

bool known(const std::string& name)
{
    return handlers().count(name) != 0;
}

bool is_synchronous(const std::string& name)
{
    static const std::set<std::string> synchronous{"list_projects", "list_project_sessions", "list_outputs",
                                                   "read_output", "search_output", "queue", "sent", "specs",
                                                   "gate_list", "gate_history"};
    return synchronous.count(name) != 0;
}

std::string call(const std::string& name, const Value& args)
{
    return handlers().at(name)(args);
}

}
