#ifndef PONG_SCENE_HH_
#define PONG_SCENE_HH_

#include <engine/scene.hh>
#include <gameobjects/quad.hh>
#include <ecs/entity.hh>

class PongScene : public Scene {
public:
    void OnStart() override;
    void OnUpdate() override;
    void OnEvent(const sapp_event *event) override;
    void OnEnd() override;
    void OnTick(float tick_t) override;

    void ResetRound();

    entity_id player = 0;
    entity_id enemy  = 1;
    entity_id ball   = 2;

    glm::vec2 ballVelocity = glm::vec2(0.0f);
private:
    int playerPoints = 0;
    int enemyPoints = 0;
};


#endif
