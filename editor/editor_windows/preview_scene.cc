//
// Created by Siddhant Madhur on 26/03/2026.
//

#include "preview_scene.h"

#include "gameobjects/quad.hh"

#define ZOOM 4.0f
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
        { // Background quad
            auto bg = ecs->createEntity();
            Quad q = Quad(glm::vec3(0.0, 0.0, 0.0), glm::vec3(width_s, height_s, 1), glm::vec4(0.0, 0.0, 0.0, 1.0));
            ecs->AddProperty(bg, q.mesh);
            ecs->AddProperty(bg, q.position);
        }
        { // CAMERA

            Camera playerCam = Camera(width_s / height_s);
            playerCam.configure2D(width_s, height_s, ZOOM);
            SetPlayerCamera(playerCam);
        }
        {
            auto block = ecs->createEntity();
            Quad q = Quad(glm::vec3(0.0, 0.0, 1.0), glm::vec3(100, 100, 1), glm::vec4(1.0, 1.0, 1.0, 1.0));
            ecs->AddProperty(block, q.mesh);
            ecs->AddProperty(block, q.position);
        }
    }
    void GamePreviewScene::OnEvent(const sapp_event *event) {};
    void GamePreviewScene::OnDestroy() {};
    void GamePreviewScene::OnFrameUpdate() {};
    void GamePreviewScene::OnTickUpdate(float tick_t) {};

    void GamePreviewScene::SetResolution(glm::vec2 new_res) {
        resolution = new_res;
    };
}

