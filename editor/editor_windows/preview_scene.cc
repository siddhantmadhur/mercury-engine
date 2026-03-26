//
// Created by Siddhant Madhur on 26/03/2026.
//

#include "preview_scene.h"

#include "gameobjects/quad.hh"

void GamePreviewScene::OnStart() {
    auto bg = ecs.createEntity();
    Quad q = Quad(glm::vec2(0.0, 0.0), glm::vec2(1920, 1080), glm::vec4(0.0, 0.0, 0.0, 1.0));
    ecs.addMesh(bg, q.mesh);
    ecs.addPosition(bg, q.position);
}
void GamePreviewScene::OnEvent(const sapp_event *event) {};
void GamePreviewScene::OnDestroy() {};
void GamePreviewScene::OnFrameUpdate() {};
void GamePreviewScene::OnTickUpdate(float tick_t) {};
