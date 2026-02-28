#ifndef MERCURY_GAMEOBJECT_IMPL_HH
#define MERCURY_GAMEOBJECT_IMPL_HH

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <sokol/sokol_gfx.h>
#include <shaders/generic_texture.glsl.hh>

typedef struct vertex_t {
    float x, y, z;
    int16_t u, v;
    float r, g, b, a;
} vertex_t;


// TODO: allow two gameobjects to combine their vertices so that the no. of draw calls is reduced
class GameObject {
public:
    void setId(uint16_t);
    void SetColor(glm::vec4);
    void UseTexture(bool);
    std::vector<vertex_t> getVertices(); 
    std::vector<uint16_t> getIndices(); 
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
    bool isTexture();
    void Move(glm::vec3 position);
    uint16_t id; // indices id for rendering
    void Draw();
    glm::vec3 GetPosition();
    void SetPosition(glm::vec3);
private:
    glm::vec4 color;
    bool use_texture;
    glm::vec3 position = glm::vec3(0.0f);

};

#endif
