#ifndef MERCURY_GAMEOBJECT_IMPL_HH
#define MERCURY_GAMEOBJECT_IMPL_HH

#include <cstdint>
#include <vector>

typedef struct vertex_t {
    float x, y, z;
    int16_t u, v;
} vertex_t;

class GameObject {
public:
    void setId(uint16_t);
    std::vector<vertex_t> getVertices(); 
    std::vector<uint16_t> getIndices(); 
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
private:
    uint16_t id;
};

#endif
