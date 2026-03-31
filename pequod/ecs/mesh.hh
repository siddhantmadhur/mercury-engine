#ifndef PEQUOD_MESH_IMPL_HH_
#define PEQUOD_MESH_IMPL_HH_

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "pobject/property.h"

namespace Pequod {
    typedef struct vertex_t {
        float x, y, z;
        int16_t u, v;
        float r, g, b, a;
    } vertex_t;


    // TODO: allow two gameobjects to combine their vertices so that the no. of draw calls is reduced
    class Mesh {
    public:
        Mesh(std::vector<vertex_t>, std::vector<uint16_t>, glm::vec3);
        ~Mesh();
        std::vector<vertex_t> vertices;
        std::vector<uint16_t> indices;
        uint32_t vertices_id = 0;   // the id to get this in the ECS vertex vector
        uint32_t indices_id = 0;    // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ indices vector
        glm::vec3 scale;
    };
}
#endif
