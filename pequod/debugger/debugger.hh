#ifndef PEQUOD_DEBUGGER_IMPL_HH_
#define PEQUOD_DEBUGGER_IMPL_HH_

#include <istream>
namespace PDebug {
    void log(std::string);
    void warn(std::string);
    void error(std::string);
    void info(std::string);
};

#endif
