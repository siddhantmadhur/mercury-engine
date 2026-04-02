#include "properties/position.h"
#include "engine/world_scene.h"
#include "glm/ext/matrix_transform.hpp"
#include "rigidbody/rigidbody.hh"
#include <ecs/ecs.hh>
#include <format>
#include <iostream>
#include <type_traits>
#include <debugger/debugger.hh>
#include <unordered_map>

#define PEQUOD_INTERPOLATE_MOVEMENT 0

#include "pobject/pobject.h"

namespace Pequod {
    ECS::ECS() {
        vertices = std::vector<vertex_t>();
        indices = std::vector<uint16_t>();
    }

    ECS::~ECS() {
    }

    template<class TProperty>
    std::unordered_map<std::type_index, std::unordered_map<entity_id, std::shared_ptr<TProperty> > > ECS::properties;

    void ECS::Disable(entity_id id) {
        auto mesh = GetProperty<Mesh>(id);
        if (mesh) {
            auto &meshes = GetProperties<Mesh>();
            meshes.erase(id);
        }

        auto pos = GetProperty<Position>(id);
        if (pos) {
            auto &positions = GetProperties<Position>();
            positions.erase(id);
        }
    }

    void ECS::ResetBuffers() {
        vertices.clear();
        indices.clear();
    }


    void ECS::addMesh(entity_id id, std::shared_ptr<Mesh> mesh) {
        auto &meshes = GetProperties<Mesh>();
        if (meshes.contains(id)) {
            std::cout << "Mesh already added" << std::endl;
            return;
        }

        meshes[id] = mesh;

        mesh->indices_id = indices.size();
        mesh->vertices_id = vertices.size();

        PDebug::log(std::format("addMesh: id={} vertices={} indices={} scale=({},{},{})",
                                id, mesh->vertices.size(), mesh->indices.size(),
                                mesh->GetScale().x, mesh->GetScale().y, mesh->GetScale().z));

        size_t prev_size = vertices.size();
        if (prev_size > 0) {
            for (int i = 0; i < mesh->indices.size(); i++) {
                mesh->indices[i] += prev_size;
            }
        }

        // std::cout << "i: " << mesh->indices[i] << std::endl;
        this->vertices.insert(vertices.end(), mesh->vertices.begin(), mesh->vertices.end());
        this->indices.insert(indices.end(), mesh->indices.begin(), mesh->indices.end());
    }


    void ECS::setupRender(sg_bindings &bind) {
        size_t vertex_size = vertices.size() * sizeof(vertex_t);
        if (vertices.size() > 0) {
            sg_update_buffer(bind.vertex_buffers[0], (sg_range){
                                 .ptr = vertices.data(),
                                 .size = vertex_size,
                             });
        }

        size_t indices_size = indices.size() * sizeof(uint16_t);
        if (indices.size() > 0) {
            sg_update_buffer(bind.index_buffer, (sg_range){
                                 .ptr = indices.data(),
                                 .size = indices_size,
                             });
        }
    }


    void ECS::render(Camera &cam, float delta_t) {
        static int render_log_count = 0;
        if (render_log_count < 3) {
            PDebug::log(std::format("render: max_id={} total_vertices={} total_indices={}",
                                    max_id, vertices.size(), indices.size()));
            render_log_count++;
        }
        for (int i = 0; i < max_id; i++) {
            auto &meshes = GetProperties<Mesh>();
            // [CLAUDE] TODO: meshes[i] silently creates a default entry if key doesn't exist — use .find() or .contains() check
            auto mesh = meshes[i];
            auto &positions = GetProperties<Position>();
            auto position = positions[i];

            if (mesh == nullptr || position == nullptr) {
                continue;
            }


#if PEQUOD_INTERPOLATE_MOVEMENT
            glm::vec3 diff = position->position - position->raw_position;
            if (delta_t > 0) {
                //positions[i]->raw_position = positions[i]->position;
                //PDebug::log(std::format("POSITION: {}, {}", diff.x, diff.y));
                position->raw_position += diff / delta_t;
            } else {
                //position->raw_position += diff;
                position->raw_position = position->position;
            }
#else
#endif
            glm::vec3 pos = position->Get();


            //std::cout << "ID: " << i << std::endl;
            //std::cout << mesh->indices_id << ", " << mesh->indices.size() << std::endl;
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, pos);
            model_params_t new_params;
            new_params.model = model;
            new_params.use_texture0 = false ? 1.0f : 0.0f;
            new_params.scale = mesh->GetScale();
            sg_apply_uniforms(UB_model_params, SG_RANGE(new_params));
            sg_draw(mesh->GetIndicesID(), mesh->GetIndices().size(), 1);
        }
    }

    /**
    #define ADD_PROPERTY(prop) template void ECS::AddProperty<prop>(entity_id, std::shared_ptr<prop>)
    ADD_PROPERTY(Position);
    ADD_PROPERTY(Mesh);
    **/
}
