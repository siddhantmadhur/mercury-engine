//
// Created by Siddhant Madhur on 26/03/2026.
//

#include "preview_scene.h"

#include <pobject/nodes/box2d.h>

#define ZOOM 1.0f
#define FAKE_RES 1
#if FAKE_RES
#define height_s 1080.0f
#define width_s 1920.0f
#else
#define height_s (sapp_heightf() * (1.0f / ZOOM))
#define width_s (sapp_widthf() * (1.0f / ZOOM))
#endif

namespace Pequod {
void GamePreviewScene::OnStart() {
  {
    // Background quad
    std::shared_ptr<PObject> bg = object_manager->NewObject<Box2D>(
        glm::vec2(0.0f), glm::vec2(width_s, height_s), glm::vec4(0, 0, 0, 1));
  }
  {
    // CAMERA
    Camera playerCam = Camera(width_s / height_s);
    playerCam.configure2D(width_s, height_s, ZOOM);
    SetPlayerCamera(playerCam);
  }
  {
    std::shared_ptr<PObject> block = object_manager->NewObject<Box2D>(
        glm::vec2(0.0f), glm::vec2(100.0f, 100.0f), glm::vec4(1.0f));
    std::shared_ptr<PObject> child = object_manager->NewObject<Box2D>(
        block, glm::vec2(200.0f), glm::vec2(100.0f, 100.0f), glm::vec4(1.0f));
  }
}

void GamePreviewScene::OnEvent(const sapp_event *event) {};

void GamePreviewScene::OnDestroy() {};

void GamePreviewScene::OnFrameUpdate() {};

void GamePreviewScene::OnTickUpdate(float tick_t) {};

void GamePreviewScene::SetResolution(glm::vec2 new_res) {
  resolution = new_res;
};
}  // namespace Pequod
