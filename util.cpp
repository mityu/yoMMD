#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstdlib>
#include "yommd.hpp"
#include "platform.hpp"

namespace {
std::filesystem::path getHomePath();
namespace globals {
static constexpr char usage[] = R"(
Usage: yommd <options>

options:
    --config <toml>     Specify config file
    --logfile <file>    Output logs to <file>
    -h|--help           Show this help
)";
const std::filesystem::path homePath = getHomePath();
}
}

CmdArgs CmdArgs::Parse(const std::vector<std::string>& args) {
    if (args.empty()) {
        Err::Exit("Executable file name must be passed.");
    }

    std::filesystem::path executable(args[0]);
    CmdArgs cmdArgs;

    cmdArgs.cwd = executable.parent_path();

    const auto end = args.cend();
    auto itr = ++args.cbegin();  // The first item is the executable path. Skip it.
    while (itr != end) {
        if (*itr == "-h" || *itr == "--help") {
            Info::Log(globals::usage);
            std::exit(0);
        } else if (*itr == "--config") {
            if (++itr == end) {
                Err::Log("No toml file name specified after \"--config\"");
                Err::Exit(globals::usage);
            } else if (!cmdArgs.configFile.empty()) {
                Err::Log("Multiple config file detected.  Use the last one.");
            }
            cmdArgs.configFile = *itr;
        } else if (*itr == "--logfile") {
            if (++itr == end) {
                Err::Log("No log file name specified after \"--logfile\"");
                Err::Exit(globals::usage);
            } else if (!cmdArgs.logFile.empty()) {
                Err::Log("Multiple log file specified.  Use the last one.");
            }
            cmdArgs.logFile = *itr;
        } else {
            Err::Exit("Unknown option:", *itr, '\n', globals::usage);
        }
        ++itr;
    }

    // Fallback
    if (cmdArgs.logFile.empty())
        cmdArgs.logFile = Constant::DefaultLogFilePath;

    // Make absolute if necessary.
    if (!cmdArgs.configFile.empty())
        Yommd::makeAbsolute(cmdArgs.configFile, cmdArgs.cwd);

    if (!cmdArgs.logFile.empty())
        Yommd::makeAbsolute(cmdArgs.logFile, cmdArgs.cwd);

    Info::Log(cmdArgs.configFile, cmdArgs.logFile);

    return cmdArgs;
}

namespace Yommd {
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
        ss << "\nAborting because of panic.";
        Err::Exit(ss.str());
    } else {
        Err::Log(ss.str());
    }
}

void makeAbsolute(std::filesystem::path& path, const std::filesystem::path& cwd) {
    if (path.is_absolute())
        return;
    else if (const auto u8path = path.generic_u8string(); u8path.starts_with(u8"~/"))
        path = globals::homePath / std::filesystem::path(u8path.substr(2));
    else
        path = cwd / path;
}

}

namespace {
std::filesystem::path getHomePath() {
#ifdef PLATFORM_WINDOWS
    const std::wstring_view wpath = _wgetenv(L"USERPROFILE");
    if (wpath.empty())
        Err::Exit("%USERPROFILE% is not set");
    return std::filesystem::path(String::wideToMulti<char8_t>(wpath));
#else
    const std::string_view path = std::getenv("HOME");
    if (path.empty())
        Err::Exit("$HOME is not set");
    return std::filesystem::path(String::tou8(path));
#endif
}
}
