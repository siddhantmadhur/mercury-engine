#ifndef PONG_SCENE_HH_
#define PONG_SCENE_HH_

#include <engine/scene.hh>
#include <gameobjects/quad.hh>

class PongScene : public Scene {
public:
    void OnStart() override;
    void OnUpdate() override;
    void OnEvent(const sapp_event *event) override;
    void OnEnd() override;
    Shapes::Quad* player = NULL;
    Shapes::Quad* enemy = NULL;
    Shapes::Quad* ball = NULL;
    glm::vec2 ballVector;
    int playerPoints = 0;
    int enemyPoints = 0;
    float enemyDx = 0.0f;
    int ticks = 0;
private:
    glm::vec2 direction;
};


#endif
