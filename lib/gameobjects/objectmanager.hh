#ifndef MERCURY_OBJECT_MANAGER_IMPL_HH
#define MERCURY_OBJECT_MANAGER_IMPL_HH

#include <gameobjects/gameobject.hh>

#include <cstdint>

class ObjectManager {
public:
    ObjectManager();
    ~ObjectManager();
    void add(GameObject& obj);
    void render(GameObject& obj);
    
private:
    vertex_t* vertices;
    uint16_t current_size;
    uint16_t max_size;
    uint16_t length;
};
#endif
