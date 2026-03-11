/**
* editor_project.hh
*
* This handles selecting a project and loading it for the next scene
*
**/
#ifndef PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_
#define PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_

#include <editor_windows/editor_scene.hh>
#include <string>

namespace Pequod {
class ProjectSelectionScene : public EditorScene {
public:
    void OnInitialLoad() override;
    void OnEventUpdate(const sapp_event* event) override;
    void OnFrameUpdate() override;
    void OnTickUpdate() override;
    void OnDestroy() override;
private:
   
    bool show_new_project_window = false;
    char* new_project_name;
    char* project_directory;
    
    std::string imgui_configuration_path;
};
}

#endif
