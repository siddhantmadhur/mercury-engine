#include "ecs/ecs.hh"
#include <gameobjects/gameobject.hh>

GameObject::GameObject() {
}

void GameObject::NewECS(ECS& ecs) {
    entity_id id = ecs.createEntity();
    ecs.addMesh(id, this->mesh);    
    //ecs.addPosition(id, &this->mesh);    
}
