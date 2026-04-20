/**
 * editor_project.hh
 *
 * The main engine editor scene which handles calling other functions to form
 * the basis of the application via its windows.
 *
 **/
#ifndef PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_
#define PEQUOD_EDITOR_PROJECT_SCENE_IMPL_HH_

#include <editor_windows/editor_scene.hh>
#include <memory>
#include <project_handler/project_handler.hh>
#include <string>

#include "engine/world_scene.h"
#include "panel/file_explorer.hh"
#include "panel/object_properties.h"
#include "panel/object_tree.h"

namespace Pequod {
class ProjectSelectionScene : public EditorScene {
 public:
  void OnInitialLoad() override;

  void OnEventUpdate(const sapp_event *event) override;

  void OnFrameUpdate() override;

  void OnTickUpdate() override;

  void OnDestroy() override;

  void RenderScenePreview(WorldScene **scene) override;

 private:
  void CreateProject();  // Creates new project files
  void OpenProject();    // Opens project files
  void SaveScene();      // Saves current scene to XML

  void ToggleNewProjectWin();   // Toggles the wizard window to create a project
  void ToggleOpenProjectWin();  // Toggles the wizard window to open a project
  void QuitProgram();

  std::unique_ptr<ProjectHandler> current_project_handler;

  bool show_new_project_window = false;
  bool open_prev_project_window = false;
  char *new_project_name;
  char *project_directory;

  std::string imgui_configuration_path;

  FileExplorer explorer;
  std::unique_ptr<ObjectTree> object_tree = nullptr;
  std::unique_ptr<ObjectPropertiesPanel> object_properties = nullptr;
  entity_id selected_entity = 0;  // 0 - root / none, 1> - some entity
};
}  // namespace Pequod

#endif
