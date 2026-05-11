//
// Created by smadhur on 5/10/2026.
//

#ifndef PEQUOD_ENGINE_SCENE_H
#define PEQUOD_ENGINE_SCENE_H

#include <scene/scene.h>

namespace Pequod {
class UIEditorScene : public GameScene {
public:
  void OnStart() override;
  void OnDestroy() override;
  void OnFrame(double delta_t) override;
  void OnTick(double delta_t) override;

private:
  kEntityId horizontal_slice_left = -1;
  kEntityId horizontal_slice_right = -1;
  kEntityId vertical_slice_left = -1;
  kEntityId vertical_slice_right = -1;
  float camera_zoom = 1.0;
  float horizontal_ratio = 0.33;
  float vertical_ratio = 0.33;

};
}

#endif //PEQUOD_ENGINE_SCENE_H
