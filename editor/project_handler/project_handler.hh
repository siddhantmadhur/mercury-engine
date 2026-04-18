/**
* project_handler.hh
*
* This file is responsible for creating and parsing *project* information
* 
* That information pertains to what version a project was made on, what
* scene it includes etc.
*
**/

#ifndef PEQUOD_PROJECT_HANDLER_IMPL_HH_
#define PEQUOD_PROJECT_HANDLER_IMPL_HH_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <os/filesystem.h>

namespace Pequod {
    class ProjectHandler {
    public:
        ProjectHandler(fs::path);

        ~ProjectHandler();

        void Load(fs::path proj_directory);

        void Create(std::string name);

        void Save();

        std::vector<fs::path> GetProjects();

        void ReloadProjects();

        bool has_loaded_project = false;
        fs::path project_path;

    private:
        // semantic versioning: (major).(minor).(patch)
        uint16_t project_version_major;
        uint16_t project_version_minor;
        uint16_t project_version_patch;

        std::string project_name;
        fs::path directory_path;

        std::vector<fs::path> projects_list;
    };
} // namespace Pequod

#endif
