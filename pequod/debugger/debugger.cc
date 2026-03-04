#include <ctime>
#include <chrono>
#include <debugger/debugger.hh>
#include <iostream>

void PDebug::log(std::string inputString) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm time_struct = *std::localtime(&now_c);

    std::cout << "[DEBUG]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << inputString << std::endl;
}

void PDebug::warn(std::string inputString) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm time_struct = *std::localtime(&now_c);

    std::cout << "[WARN]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << inputString << std::endl;
}
void PDebug::info(std::string inputString) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm time_struct = *std::localtime(&now_c);

    std::cout << "[INFO]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << inputString << std::endl;
}

void PDebug::error(std::string inputString) {
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm time_struct = *std::localtime(&now_c);

    std::cerr << "[ERROR]" << std::put_time(&time_struct, " %Y-%m-%d %H:%M:%S ") << inputString << std::endl;
}
