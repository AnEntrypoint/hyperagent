#include "gate/command.hpp"

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/text.hpp"
#include "gate/gate.hpp"

namespace hyperagent::gate {

namespace {

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

void add_command(CLI::App& app)
{
    auto* group = app.add_subcommand("gate", "gate tickets: run a command under a turn, list live tickets, show history");
    group->require_subcommand(1);

    auto words = std::make_shared<std::vector<std::string>>();
    auto* command_run = group->add_subcommand("run", "wait for a ticket, then run: gate run <label> -- <command> [args]");
    command_run->add_option("words", *words, "label followed by the command")->required();
    command_run->callback([words]() {
        if (words->size() < 2) {
            std::cerr << "usage: gate run <label> -- <command> [args]\n";
            std::exit(2);
        }
        const std::vector<std::string> command(words->begin() + 1, words->end());
        try {
            std::exit(run(words->front(), command));
        } catch (const std::exception& failure) {
            std::cerr << failure.what() << "\n";
            std::exit(2);
        }
    });

    group->add_subcommand("list", "live gate tickets in turn order")->callback([]() {
        emit([]() { return core::text::join_lines(list_lines()); });
    });

    group->add_subcommand("history", "gate history per owner: windows, waited and ran seconds, longest run")
        ->callback([]() { emit([]() { return core::text::join_lines(history_lines()); }); });
}

}
