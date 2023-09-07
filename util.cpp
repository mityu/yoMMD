#include <string>
#include <sstream>
#include <string_view>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include "util.hpp"
#include "constant.hpp"
#include "platform.hpp"

// Forward declaration for functions in auto/version.cpp
namespace Version {
std::string_view getString();
}

namespace {
std::filesystem::path getHomePath();
namespace globals {
constexpr std::string_view usage = R"(
Usage: yommd <options>

options:
    --config <toml>     Specify config file
    --logfile <file>    Output logs to <file>
    -v|--version        Show software version
    -h|--help           Show this help
)";
}
}

CmdArgs CmdArgs::Parse(const std::vector<std::string>& args) {
    if (args.empty()) {
        Err::Exit("Executable file name must be passed.");
    }

    std::filesystem::path executable(args[0]);
    CmdArgs cmdArgs;

    const auto end = args.cend();
    auto itr = ++args.cbegin();  // The first item is the executable path. Skip it.
    while (itr != end) {
        if (*itr == "-h" || *itr == "--help") {
            Info::Log(globals::usage);
            std::exit(0);
        } else if (*itr == "-v" || *itr == "--version") {
            Info::Log("version:", Version::getString());
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
        cmdArgs.configFile = Yommd::makeAbsolute(
                cmdArgs.configFile, ::Path::getWorkingDirectory());

    if (!cmdArgs.logFile.empty())
        cmdArgs.logFile = Yommd::makeAbsolute(
                cmdArgs.logFile, ::Path::getWorkingDirectory());

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

std::filesystem::path makeAbsolute(
        const std::filesystem::path& path, const std::filesystem::path& cwd) {
    namespace fs = std::filesystem;
    static const auto homePath = getHomePath();
    if (path.is_absolute())
        return path;
    else if (const auto u8path = path.generic_u8string(); u8path.starts_with(u8"~/"))
        return fs::weakly_canonical(homePath / fs::path(u8path.substr(2)));
    else
        return fs::weakly_canonical(cwd / path);
}

}

namespace Path {
std::filesystem::path getWorkingDirectory() {
    // TODO: Can I truely initialize this here?
    static const auto cwd = std::filesystem::current_path();
    return cwd;
}
}

namespace {
std::filesystem::path getHomePath() {
#ifdef PLATFORM_WINDOWS
    const wchar_t *wpath = _wgetenv(L"USERPROFILE");
    if (!wpath)
        Err::Exit("%USERPROFILE% is not set");
    return std::filesystem::path(String::wideToMulti<char8_t>(wpath));
#else
    const char *path = std::getenv("HOME");
    if (!path)
        Err::Exit("$HOME is not set");
    return std::filesystem::path(String::tou8(std::string_view(path)));
#endif
}
}
