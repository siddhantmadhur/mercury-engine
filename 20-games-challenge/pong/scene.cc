#include "scene.h"

#include "pobject/nodes/box2d.h"

#define ZOOM 1.0f

#define heights (sapp_heightf() * (1 / ZOOM))
#define widths (sapp_widthf() * (1 / ZOOM))

using namespace Pequod;

void PongScene::ResetRound()
{
}

void PongScene::OnStart()
{
    {
        // CAMERA
        Camera playerCam = Camera(width_s / height_s);
        playerCam.configure2D(width_s, height_s, ZOOM);
        SetPlayerCamera(playerCam);
    }

    auto offset_from_center = glm::vec2((width_s / 2.0f) - 64.0f, 0.0f);
    {
        player =
            object_manager->NewObject<Box2D>(-offset_from_center, glm::vec2(24.0f, 96.0f), glm::vec4(1.0f));
    }
    {
        enemy =
            object_manager->NewObject<Box2D>(offset_from_center, glm::vec2(24.0f, 96.0f), glm::vec4(1.0f));
    }
    {
        ball =
            object_manager->NewObject<Box2D>(glm::vec2(0.0f), glm::vec2(24.0f, 24.0f), glm::vec4(1.0f));
    }
}

void PongScene::OnDestroy()
{
}

void PongScene::OnEvent(const sapp_event* event)
{
}

#define SPEED 0.8f


void PongScene::OnTickUpdate(float tick_t)
{
    if (IsKeyPressed(SAPP_KEYCODE_ESCAPE))
    {
        sapp_quit();
    }

    if (IsKeyPressed(SAPP_KEYCODE_W))
    {
        auto pos = player->Get<Position>();
        pos->Set(pos->Get() + glm::vec3(0.0f, SPEED * tick_t, 0.0f));
    }
    if (IsKeyPressed(SAPP_KEYCODE_S))
    {
        auto pos = player->Get<Position>();
        pos->Set(pos->Get() + glm::vec3(0.0f, -SPEED * tick_t, 0.0f));
    }
    
    if (IsKeyPressed(SAPP_KEYCODE_UP))
    {
        auto pos = enemy->Get<Position>();
        pos->Set(pos->Get() + glm::vec3(0.0f, SPEED * tick_t, 0.0f));
    }
    if (IsKeyPressed(SAPP_KEYCODE_DOWN))
    {
        auto pos = enemy->Get<Position>();
        pos->Set(pos->Get() + glm::vec3(0.0f, -SPEED * tick_t, 0.0f));
    }
}

void PongScene::OnFrameUpdate(float delta_t)
{
}
