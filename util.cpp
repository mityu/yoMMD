#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstdlib>
#include "yommd.hpp"

namespace Yommd {
static constexpr char usage[] = R"(
Usage: yommd <options>

options:
    --config <toml>     Specify config file
    --logfile <file>    Output logs to <file>
    -h|--help           Show this help
)";

void parseArgs(const std::vector<std::string>& args) {
    if (args.empty()) {
        Err::Exit(usage);
    }

    const auto end = args.cend();
    auto itr = args.cbegin();
    while (itr != end) {
        if (*itr == "-h" || *itr == "--help") {
            Info::Log(usage);
            std::exit(0);
        } else if (*itr == "--config") {
            if(++itr == end) {
                Err::Log("No toml file name specified after \"--config\"");
                Err::Exit(usage);
            }
            // TODO: Detect multiple config files and give error for duplications.
            // TODO: Set config file.
        } else {
            Err::Exit("Unknown option:", *itr, '\n', usage);
        }
    }
}

void slogFunc(const char *tag, uint32_t logLevel, uint32_t logItem, const char *message, uint32_t linenr, const char *filename, void *user_data) {
    (void)user_data;

    std::stringstream ss;

    if (tag) {
        ss << '[' << tag << ']';
    }

    switch (logLevel) {
    case 0: ss << "panic:"; break;
    case 1: ss << "error:"; break;
    case 2: ss << "warning:"; break;
    default: ss << "info:"; break;
    }

    ss << " [id:" << logItem << ']';
    if (filename) {
        ss << ' ';
#if defined(_MSC_VER)
        ss << filename << '(' << linenr << "): ";  // MSVC compiler error format
#else
        ss << filename << ':' << linenr << ":0: ";  // gcc/clang compiler error format
#endif
    } else {
        ss << "[line:" << linenr << "] ";
    }

    if (message) {
        ss << "\n\t" << message;
    }

    if (logLevel == 0) {
        ss << "Aborting because of panic.";
        Err::Exit(ss.str());
    } else {
        Err::Log(ss.str());
    }
}
}
