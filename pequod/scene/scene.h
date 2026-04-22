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

#include "gameobjects/camera.hh"
#include "input/input_manager.h"
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

  bool GetCameraProj(glm::mat4x4&);

  std::vector<Primitive> GetPrimitives();

  float GetWidth() const;
  float GetHeight() const;
  void SetWidth(float);
  void SetHeight(float);

  void SetInputManager(InputManager*);

  void QuitScene();
  bool ShouldQuit() const;

 protected:
  std::unique_ptr<PObjectManager> object_manager_ = nullptr;
  std::unique_ptr<Camera> player_camera_ = nullptr;
  InputManager* input_manager_ = nullptr;

 private:
  float width_ = 0.0;
  float height_ = 0.0;
  bool should_close = false;
};

}  // namespace Pequod

#endif  // PEQUODENGINE_SCENE_H
