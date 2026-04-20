/*
 * scene.h
 *
 * Responsible for:
 * - handling objects
 * - handling properties
 * - creating vertex buffers
 * - handling models
 *
 */

#ifndef PEQUODENGINE_SCENE_H
#define PEQUODENGINE_SCENE_H
#include <memory>

#include "pobject/manager.h"

namespace Pequod {

class GameScene {
 public:
  virtual ~GameScene() = default;
  GameScene();

  virtual void OnStart() = 0;
  virtual void OnFrame(double delta_t) = 0;  // delta time in ms
  virtual void OnTick(double delta_t) = 0;   // delta time in ms
  virtual void OnDestroy() = 0;

  std::vector<Primitive> GetPrimitives();

 protected:
  std::unique_ptr<PObjectManager> object_manager_ = nullptr;

 private:
};

}  // namespace Pequod

#endif  // PEQUODENGINE_SCENE_H
