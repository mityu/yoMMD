#ifndef PLATFORM_HPP_
#define PLATFORM_HPP_

#if defined(_WIN32)
#define PLATFORM_WINDOWS
#elif defined(__linux__)
#define PLATFORM_LINUX
#elif defined(__clang__)
#if defined(__APPLE__) || defined(__OSX__)
#define PLATFORM_MAC
#endif
#endif

#if !(defined(PLATFORM_MAC) || defined(PLATFORM_WINDOWS) || defined(PLATFORM_LINUX))
#error "Failed to detect platform."
#endif

#endif
