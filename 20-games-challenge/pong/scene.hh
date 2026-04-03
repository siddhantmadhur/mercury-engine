#ifndef PONG_SCENE_HH_
#define PONG_SCENE_HH_

#include <Jolt/Jolt.h>
#include "Jolt/Physics/Body/BodyID.h"
#include <ecs/entity.hh>

#include "engine/world_scene.h"

class PongScene : public Pequod::WorldScene {
public:
    void OnStart() override;
    void OnFrameUpdate() override;
    void OnEvent(const sapp_event *event) override;
    void OnDestroy() override;
    void OnTickUpdate(float tick_t) override;

    void ResetRound();

    entity_id player = 0;
    entity_id enemy  = 1;
    entity_id ball   = 2;

    JPH::BodyID player_id;
    JPH::BodyID ball_id;

    glm::vec2 ballVelocity = glm::vec2(0.0f);


private:
    int playerPoints = 0;
    int enemyPoints = 0;
};


#endif
