//
// Created by smadhur on 5/10/2026.
//

#ifndef MERCURY_ENGINE_SCENE_H
#define MERCURY_ENGINE_SCENE_H

#include <scene/scene.h>

namespace Mercury {
class UIEditorScene : public GameScene {
 public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

 private:
  kEntityId horizontal_slice_left = entt::null;
  kEntityId horizontal_slice_right = entt::null;
  kEntityId vertical_slice_left = entt::null;
  kEntityId vertical_slice_right = entt::null;
  float camera_zoom = 1.0;
  float horizontal_ratio = 0.33;
  float vertical_ratio = 0.33;
};
}  // namespace Mercury

#endif  // MERCURY_ENGINE_SCENE_H
