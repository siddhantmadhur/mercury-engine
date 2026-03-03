#ifndef PEQUOD_MESH_IMPL_HH_
#define PEQUOD_MESH_IMPL_HH_

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
namespace Pequod {
class Mesh {
public:
    Mesh();
    ~Mesh();
    std::vector<vertex_t> vertices;
    std::vector<uint16_t> indices;
private:
};
}

#endif
