#ifndef PONG_SCENE_HH_
#define PONG_SCENE_HH_

#include <engine/scene.hh>

class PongScene : public Scene {
public:
    void OnStart() override;
    void OnUpdate() override;
    void OnEvent(const sapp_event *event) override;
    GameObject* player = NULL;
private:
    glm::vec2 direction;
};


#endif
