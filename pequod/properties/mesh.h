#ifndef PEQUOD_MESH_IMPL_HH_
#define PEQUOD_MESH_IMPL_HH_

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

#include "property.h"

namespace Pequod {
    typedef struct vertex_t {
        float x, y, z;
        int16_t u, v;
        float r, g, b, a;
    } vertex_t;


    // TODO: allow two gameobjects to combine their vertices so that the no. of draw calls is reduced
    class Mesh : public Property {
    public:
        Mesh();

        ~Mesh();

        void SetVertices(std::vector<vertex_t>);

        void SetIndicies(std::vector<uint16_t>);

        void SetScale(glm::vec3);

        glm::vec3 GetScale();

        uint32_t GetIndicesID();

        const std::vector<uint16_t> GetIndices();

        std::vector<vertex_t> vertices;
        std::vector<uint16_t> indices;
        uint32_t vertices_id = 0; // the id to get this in the ECS vertex vector
        uint32_t indices_id = 0; // ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ indices vector
    private:
        glm::vec3 scale;
    };
}
#endif
