/**
* project_handler.hh
*
* this file is responsible for creating and parsing *project* information
* 
* that information pertains to what version a project was made on, what
* scene it includes etc.
*
**/

#ifndef PEQUOD_PROJECT_HANDLER_IMPL_HH_
#define PEQUOD_PROJECT_HANDLER_IMPL_HH_

#include <cstdint>
#include <string>

namespace Pequod {

class Project {

public:
    Project();
    void Load(std::string path);
    void Create(std::string name, std::string directory_path);
    void Save();

private:
    // semantic versioning: (major).(minor).(patch)
    uint16_t project_version_major;
    uint16_t project_version_minor;
    uint16_t project_version_patch;


    std::string project_name;
    std::string directory_path;

};


} // namespace Pequod

#endif
