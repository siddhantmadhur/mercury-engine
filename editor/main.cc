// mercury_editor [path/to/project.mproj] [--map res://maps/name.mmap]
//
// Opens the given project (at the given map, or its main map), or the
// project launcher when none is given.

#include <memory>
#include <string>

#include "application/default_application.h"
#include "editor_scene.h"

int main(int argc, char** argv) {
  std::filesystem::path project;
  std::string map;
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--map" && i + 1 < argc) {
      map = argv[++i];
    } else {
      project = arg;
    }
  }

  auto application = std::make_unique<Mercury::DefaultApplication>(
      "Mercury Editor", 1600, 900);
  application->SetGameScene(std::make_unique<Mercury::EditorScene>(project, map));
  return application->Run();
}
