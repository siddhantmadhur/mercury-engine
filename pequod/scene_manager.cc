
#include "scene_manager.h"

namespace Pequod {

SceneManager::SceneManager(SPtr<Application> application) {
  this->application_ = application;
}

}  // namespace Pequod