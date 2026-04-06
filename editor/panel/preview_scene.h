//
// Created by Siddhant Madhur on 26/03/2026.
//

#ifndef PEQUODENGINE_PREVIEW_SCENE_H
#define PEQUODENGINE_PREVIEW_SCENE_H
#include "engine/world_scene.h"

namespace Pequod {
    class GamePreviewScene : public WorldScene {
    public:
        void OnStart() override;

        void OnEvent(const sapp_event *event) override;

        void OnDestroy() override;

        void OnFrameUpdate() override;

        void OnTickUpdate(float tick_t) override;

        void SetResolution(glm::vec2);

    private:
        glm::vec2 resolution;
    };
}

#endif //PEQUODENGINE_PREVIEW_SCENE_H
