/**
    MERCURY : SCENE MANAGEMENT

    
**/


#ifndef MERCURY_SCENE_IMPL_HH
#define MERCURY_SCENE_IMPL_HH

#include <sokol/sokol_app.h>
#include "gameobjects/camera.hh"
#include <gameobjects/gameobject.hh>
#include <vector>


#include <sokol/sokol_gfx.h>

class Scene {
public:
    Scene();
    void Deinit();
    void Init();
    void AddObject(GameObject* obj);
    void Render(float width, float height);
    virtual void OnStart() =0; // runs at scene creation
    virtual void OnUpdate() =0; // runs once every frame
    virtual void OnEvent(const sapp_event* event) =0; // runs once every frame
    virtual void OnEnd() =0; // runs when scene is closing
    virtual void OnTick() =0; // runs every tick (rn is 20 per second) 
    void SetPlayerCamera(Camera& cam);
    Camera& GetPlayerCamera();
    void SetDelta(float delta_t);
    void SetBgColor(glm::vec4);
    bool IsKeyPressed(sapp_keycode key);
    void handleKeys(const sapp_event*event);
    float delta_t;
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
private:
    std::vector<GameObject*> objects;
    uint16_t current_id = 0;
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
    Camera playerCamera;
    uint16_t* raw_indices;
    glm::vec4 bgColor = glm::vec4(0.2f, 0.3f, 0.3f, 1.0f);
    uint16_t lastIndex;
    std::unordered_map<sapp_keycode, bool> keys_pressed;
};

#endif
