/**
* editor_project.hh
*
* This handles selecting a project and loading it for the next scene
*
**/
#ifndef PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_
#define PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_

#include <editor_windows/editor_scene.hh>
#include <memory>
#include <project_handler/project_handler.hh>
#include <string>

namespace Pequod {
class ProjectSelectionScene : public EditorScene {
public:
    void OnInitialLoad() override;
    void OnEventUpdate(const sapp_event* event) override;
    void OnFrameUpdate() override;
    void OnTickUpdate() override;
    void OnDestroy() override;
    
    void CreateNewProject();
    void OpenNewProjectWin();
    void OpenPrevProjectWin();
    void QuitProgram();

private:

    std::unique_ptr<ProjectHandler> current_project_handler;
   
    bool show_new_project_window = false;
    bool open_prev_project_window = false;
    char* new_project_name;
    char* project_directory;
    
    std::string imgui_configuration_path;
};
}

#endif
