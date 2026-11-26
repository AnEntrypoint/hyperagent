#include "cli/update.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>

#include "core/json.hpp"
#include "core/update.hpp"

namespace hyperagent::cli::update {

namespace {

core::json::Value to_json(const core::update::Result& result)
{
    core::json::Value out = core::json::Value::object();
    out["ok"] = result.ok;
    out["updated"] = result.updated;
    out["from"] = result.from;
    out["to"] = result.to;
    out["commit"] = result.commit;
    out["status"] = result.status;
    out["error"] = result.error;
    return out;
}

int run(bool automatic)
{
    const core::update::Policy policy = core::update::policy_from_environment();
    core::update::Result result;
    if (automatic && !policy.enabled) {
        result.ok = true;
        result.from = policy.current_version;
        result.to = policy.current_version;
        result.status = "auto update off: " + policy.disabled_reason;
    } else {
        result = core::update::check(policy);
    }
    std::cout << core::json::dumps(to_json(result)) << '\n';
    return result.ok ? 0 : 1;
}

}

void add_command(CLI::App& app)
{
    auto automatic = std::make_shared<bool>(false);
    auto* command = app.add_subcommand("update", "check the release manifest and install a newer build");
    command->add_flag("--auto", *automatic, "follow HYPERAGENT_AUTO_UPDATE and report nothing when it is 0");
    command->callback([automatic]() { std::exit(run(*automatic)); });
}

}
