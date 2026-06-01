//
// Created by smadhur on 6/1/2026.
//

#ifndef PEQUODENGINE_SCENE_MANAGER_H
#define PEQUODENGINE_SCENE_MANAGER_H

#include <globals.h>

namespace Pequod {

class Application;

class SceneManager {
 public:
  SceneManager(SPtr<Application>);

 private:
  SPtr<Application> application_ = nullptr;
};

}  // namespace Pequod
#endif  // PEQUODENGINE_SCENE_MANAGER_H
