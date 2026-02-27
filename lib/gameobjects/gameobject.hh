#ifndef MERCURY_GAMEOBJECT_IMPL_HH
#define MERCURY_GAMEOBJECT_IMPL_HH

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

typedef struct vertex_t {
    float x, y, z;
    int16_t u, v;
    float r, g, b, a;
    float use_texture;
} vertex_t;

class GameObject {
public:
    void setId(uint16_t);
    void SetColor(glm::vec4);
    void UseTexture(bool);
    std::vector<vertex_t> getVertices(); 
    std::vector<uint16_t> getIndices(); 
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
private:
    uint16_t id;
    glm::vec4 color;
    bool use_texture;

};

#endif
