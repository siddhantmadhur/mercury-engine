// mercury_editor [path/to/project.mproj]
//
// Opens the given project, or the project launcher when none is given.

#include <memory>

#include "application/default_application.h"
#include "editor_scene.h"

int main(int argc, char** argv) {
  std::filesystem::path project;
  if (argc > 1) project = argv[1];

  auto application = std::make_unique<Mercury::DefaultApplication>(
      "Mercury Editor", 1600, 900);
  // Panels (e.g. the UI Builder) can be dragged onto other monitors.
  application->EnableImGuiViewports(true);
  application->SetGameScene(std::make_unique<Mercury::EditorScene>(project));
  return application->Run();
}
