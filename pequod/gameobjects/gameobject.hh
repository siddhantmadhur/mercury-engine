#ifndef PEQUOD_GAMEOBJECT_IMPL_HH_
#define PEQUOD_GAMEOBJECT_IMPL_HH_

#include <typeindex>

#include "ecs/ecs.hh"
#include "ecs/mesh.hh"
#include <ecs/position.hh>
#include <vector>


class GameObject {
public:
    ~GameObject();
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Position> position;
    glm::vec2 size;
    //template <class TProperty>
    //std::shared_ptr<Property> GetProperty();
protected:
    //std::unordered_map<std::type_index, Property> properties;
};


#endif
