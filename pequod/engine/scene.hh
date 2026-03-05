/**
    MERCURY : SCENE MANAGEMENT

    
**/


#ifndef MERCURY_SCENE_IMPL_HH
#define MERCURY_SCENE_IMPL_HH

#include <sokol/sokol_app.h>
#include "gameobjects/camera.hh"
#include <ecs/ecs.hh>
#include <vector>

#include <sokol/sokol_gfx.h>

class Scene {
public:
    Scene();
    void Deinit();
    void Init();
    void Render(float width, float height);
    void simulatePhysics();
    virtual void OnStart() =0; // runs at scene creation
    virtual void OnUpdate() =0; // runs once every frame
    virtual void OnEvent(const sapp_event* event) =0; // runs once every frame
    virtual void OnEnd() =0; // runs when scene is closing
    virtual void OnTick(float tick_t) =0; // runs every tick (rn is 20 per second) 
    void SetPlayerCamera(Camera& cam);
    Camera& GetPlayerCamera();
    void SetDelta(float delta_t);
    void SetBgColor(glm::vec4);
    bool IsKeyPressed(sapp_keycode key);
    void handleKeys(const sapp_event*event);
    float delta_t; // time difference per frame
    float tick_t;  // time difference per tick
    uint64_t tick;
    ECS ecs = ECS();
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
    Camera playerCamera;
    glm::vec4 bgColor = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f);
    std::unordered_map<sapp_keycode, bool> keys_pressed;
};

#endif
