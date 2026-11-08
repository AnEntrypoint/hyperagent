#include "core/process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <stdexcept>

namespace hyperagent::core::process {

namespace {

struct Pipe {
    int read_end = -1;
    int write_end = -1;

    Pipe()
    {
        int ends[2];
        if (::pipe(ends) != 0) {
            throw std::runtime_error("pipe failed");
        }
        read_end = ends[0];
        write_end = ends[1];
    }
    ~Pipe()
    {
        if (read_end >= 0) {
            ::close(read_end);
        }
        if (write_end >= 0) {
            ::close(write_end);
        }
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
};

}

Result run_capture(const std::vector<std::string>& argv)
{
    if (argv.empty()) {
        throw std::invalid_argument("run_capture needs a program");
    }

    Pipe standard_output;
    Pipe standard_error;

    const pid_t child = ::fork();
    if (child < 0) {
        throw std::runtime_error("fork failed");
    }
    if (child == 0) {
        ::dup2(standard_output.write_end, STDOUT_FILENO);
        ::dup2(standard_error.write_end, STDERR_FILENO);
        ::close(standard_output.read_end);
        ::close(standard_error.read_end);

        std::vector<char*> arguments;
        arguments.reserve(argv.size() + 1);
        for (const auto& argument : argv) {
            arguments.push_back(const_cast<char*>(argument.c_str()));
        }
        arguments.push_back(nullptr);
        ::execvp(arguments[0], arguments.data());
        ::_exit(127);
    }

    ::close(standard_output.write_end);
    ::close(standard_error.write_end);
    standard_output.write_end = -1;
    standard_error.write_end = -1;

    std::array<int, 2> descriptors{standard_output.read_end, standard_error.read_end};
    std::array<std::string, 2> captured;
    while (descriptors[0] >= 0 || descriptors[1] >= 0) {
        std::array<pollfd, 2> watched{};
        for (std::size_t index = 0; index < 2; ++index) {
            watched[index] = pollfd{descriptors[index], POLLIN, 0};
        }
        if (::poll(watched.data(), 2, -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        for (std::size_t index = 0; index < 2; ++index) {
            if (descriptors[index] < 0 || watched[index].revents == 0) {
                continue;
            }
            std::array<char, 4096> chunk;
            const ssize_t received = ::read(descriptors[index], chunk.data(), chunk.size());
            if (received > 0) {
                captured[index].append(chunk.data(), static_cast<std::size_t>(received));
            } else if (received == 0 || errno != EINTR) {
                ::close(descriptors[index]);
                descriptors[index] = -1;
            }
        }
    }
    standard_output.read_end = -1;
    standard_error.read_end = -1;

    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    return Result{exit_code, std::move(captured[0]), std::move(captured[1])};
}

}
