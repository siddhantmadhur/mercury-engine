#ifndef PEQUOD_DEBUGGER_IMPL_H_
#define PEQUOD_DEBUGGER_IMPL_H_
#include <ctime>
#include <chrono>
#include <iomanip>
#include <iostream>

namespace PDebug
{
    template <class... Args>
    void log(std::format_string<Args...> fmt, Args&&... args) {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm time_struct = *std::localtime(&now_c);

        std::cout << "[DEBUG]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    }

    template <class... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm time_struct = *std::localtime(&now_c);

        std::cout << "[WARN]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    }

    template <class... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm time_struct = *std::localtime(&now_c);

        std::cout << "[INFO]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    }

    template <class... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm time_struct = *std::localtime(&now_c);

        std::cerr << "[ERROR]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << std::format(fmt, std::forward<Args>(args)...) << std::endl;
    }

}

#endif