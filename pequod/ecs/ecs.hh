/**
* ecs.hh
* 
* This is the ECS for an entire "scene" 
* It is a *singleton* class and hence only created and used once (again per scene)
* It handles object properties like meshes, collisions and gravity
* It does NOT handle engine properties like ticks or key events
**/

#ifndef PEQUOD_ECS_IMPL_HH_
#define PEQUOD_ECS_IMPL_HH_

#include <sokol/sokol_gfx.h>

#include "gameobjects/camera.hh"
#include <unordered_map>
#include <vector>

#include <ecs/entity.hh> 
#include <ecs/mesh.hh> 
#include <ecs/position.hh> 


class ECS {
public:
    ECS();
    ~ECS();
    entity_id createEntity(); // use to create new entities that are part of the engine  

    void addMesh(entity_id, Mesh*);
    void addPosition(entity_id, Position*);

    Mesh* getMesh(entity_id);
    Position* getPosition(entity_id);

    bool doesCollide(entity_id, entity_id);

    void setupRender(sg_bindings& bind);
    void render(Camera& cam, float delta_t);

    void processOnTick(float delta_t);
    void processOnFrame(float delta_t);
protected:
    std::unordered_map<entity_id, Mesh*> meshes;
    std::unordered_map<entity_id, Position*> positions;
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
private:
    entity_id current_id;
};

#endif
