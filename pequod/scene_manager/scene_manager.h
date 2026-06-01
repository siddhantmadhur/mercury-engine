//
// Created by smadhur on 6/1/2026.
//

#ifndef PEQUODENGINE_SCENE_MANAGER_H
#define PEQUODENGINE_SCENE_MANAGER_H

#include "os/filesystem.h"

#include <globals.h>

namespace Pequod {

class Application;

class SceneManager {
 public:
  SceneManager(Application*);

  int LoadMapFromFile(const fs::path& input_file);
  int SaveMapToFile(const fs::path& output_file);
  int LoadObjectFromFile(const fs::path& input_file);
  int SaveObjectToFile(const fs::path& output_file);

 private:
  Application* application_ = nullptr;
};

}  // namespace Pequod
#endif  // PEQUODENGINE_SCENE_MANAGER_H
