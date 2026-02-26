/**
    MERCURY : SCENE MANAGEMENT

    
**/


#ifndef MERCURY_SCENE_IMPL_HH
#define MERCURY_SCENE_IMPL_HH

#include "gameobjects/camera.hh"
#include <gameobjects/gameobject.hh>
#include <vector>


#include <sokol/sokol_gfx.h>

class Scene {
public:
    Scene();
    ~Scene();
    void Init();
    void AddObject(GameObject& obj);
    void Render(float width, float height);
    virtual void OnStart() =0; // runs at scene creation
    virtual void OnUpdate() =0; // runs once every frame
private:
    std::vector<GameObject> objects;
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
    uint16_t current_id;
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
    Camera playerCamera;
    uint16_t* raw_indices;
};

#endif
