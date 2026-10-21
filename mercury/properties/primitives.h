/*
 * primitives.h
 *
 * Built-in lit meshes for 3D objects, like Unity's GameObject > 3D Object
 * menu. Every shape is centered on the origin and sized so that the
 * Transform's scale gives its size:
 *
 *   Cube      1 x 1 x 1
 *   Sphere    diameter 1
 *   Capsule   diameter 1, height 2 (along Y)
 *   Cylinder  diameter 1, height 2 (along Y)
 *   Plane     1 x 1 on the XZ plane, facing +Y
 *   Quad      1 x 1 on the XY plane, facing +Z
 */
#ifndef MERCURY_PROPERTIES_PRIMITIVES_H
#define MERCURY_PROPERTIES_PRIMITIVES_H

#include <glm/glm.hpp>
#include <optional>
#include <string>

#include "properties/mesh.h"

namespace Mercury::Primitives {

Mesh Cube(glm::vec4 color = glm::vec4(1.0f));
Mesh Sphere(glm::vec4 color = glm::vec4(1.0f), int segments = 32,
            int rings = 16);
Mesh Capsule(glm::vec4 color = glm::vec4(1.0f), int segments = 32,
             int rings = 8);
Mesh Cylinder(glm::vec4 color = glm::vec4(1.0f), int segments = 32);
Mesh Plane(glm::vec4 color = glm::vec4(1.0f));
Mesh Quad(glm::vec4 color = glm::vec4(1.0f));

/**
 * @brief Load every mesh in a model file (.obj, .gltf, .glb, .fbx, ...) into
 * one lit mesh, keeping the file's own units and origin
 *
 * @return nullopt if the file can't be read; `error` says why
 */
std::optional<Mesh> LoadModel(const std::string& path,
                              glm::vec4 color = glm::vec4(1.0f),
                              std::string* error = nullptr);

}  // namespace Mercury::Primitives

#endif  // MERCURY_PROPERTIES_PRIMITIVES_H
