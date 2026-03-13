#include "project_handler.hh"
#include "debugger/debugger.hh"
#include <cstdlib>
#include <filesystem>
#include <format>


namespace Pequod {

ProjectHandler::ProjectHandler(fs::path root_directory_path) {
    PDebug::info("Creating project handler...");

    directory_path = root_directory_path / "pequod";

    PDebug::info(std::format("Path: {}", directory_path.string()));
}

ProjectHandler::~ProjectHandler() {
    PDebug::info("Releasing project handler...");
}

void ProjectHandler::Create(std::string name) {
    fs::path full_project_path = directory_path / std::format("{}.pdproj", name);
    fs::create_directory(full_project_path);
}

void ProjectHandler::Load(fs::path project_directory) {
    has_loaded_project = true;
    this->project_path = project_directory;
}

void ProjectHandler::Save() {}

void ProjectHandler::ReloadProjects() {
    projects_list.clear();
    for (const auto& file : fs::directory_iterator(directory_path)) {
        //PDebug::info(std::format("PATH: {}", file.path().string()));
        if (file.path().string().ends_with(".pdproj")) {
            projects_list.push_back(file.path());
        }
    }

}

std::vector<fs::path> ProjectHandler::GetProjects() {
    return projects_list;
}

} // namespace Pequod
