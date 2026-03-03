#ifndef PEQUOD_ECS_IMPL_HH_
#define PEQUOD_ECS_IMPL_HH_

#include <unordered_map>
#include <vector>

#include <ecs/entity.hh> 
#include <ecs/mesh.hh> 

namespace Pequod {
/**
* ecs.hh
* 
* This is the ECS for an entire "scene" 
* It is a *singleton* class and hence only created and used once (again per scene)
* It handles object properties like meshes, collisions and gravity
* It does NOT handle engine properties like ticks or key events
**/
class ECS {
public:
    ECS();
    ~ECS();
    void createEntity(); // use to create new entities that are part of the engine  
    void addMesh(Entity*, Mesh*);
private:
    std::unordered_map<uint64_t, Mesh*> meshes;
};
}

#endif
