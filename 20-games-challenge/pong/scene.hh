#ifndef PONG_SCENE_HH_
#define PONG_SCENE_HH_

#include <engine/scene.hh>
#include <gameobjects/quad.hh>

class PongScene : public Scene {
public:
    void OnStart() override;
    void OnUpdate() override;
    void OnEvent(const sapp_event *event) override;
    Shapes::Quad* player = NULL;
    Shapes::Quad* enemy = NULL;
    Shapes::Quad* ball = NULL;
private:
    glm::vec2 direction;
};


#endif
