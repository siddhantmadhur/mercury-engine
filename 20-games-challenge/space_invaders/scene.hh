#ifndef SPACE_INVADERS_SCENE_HH_
#define SPACE_INVADERS_SCENE_HH_

#include <engine/scene.hh>

class SpaceInvaders : public Scene {
public:
    void OnStart() override;
    void OnEnd() override;
    void OnUpdate() override;
    void OnTick(float tick_t) override;
    void OnEvent(const sapp_event* event) override;
    //void OnEnd() override;
};

#endif
