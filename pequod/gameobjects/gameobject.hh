#ifndef PEQUOD_GAMEOBJECT_IMPL_HH_
#define PEQUOD_GAMEOBJECT_IMPL_HH_

#include "ecs/ecs.hh"
#include "ecs/mesh.hh"
#include <ecs/position.hh>
#include <vector>

class GameObject {
public:
    ~GameObject();
    glm::vec3 size;
    Position* position = NULL;
    Mesh* mesh = NULL;
    RigidBody* rigid_body = NULL;
protected:
};


#endif
