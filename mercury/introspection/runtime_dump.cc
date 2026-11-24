// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "introspection/runtime_dump.h"

#include <set>

#include <nlohmann/json.hpp>

#include "components/components.h"
#include "introspection/variant_json.h"
#include "properties/transform.h"
#include "scene/map_scene.h"
#include "scripting/type_registry.h"

namespace Mercury::introspection {

namespace {

using nlohmann::json;

json Vec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

}  // namespace

std::string DumpSceneJson(MapScene& scene,
                          const std::vector<std::string>& nodes,
                          std::string* error) {
  if (error) error->clear();
  json result;
  result["nodes"] = json::array();
  result["errors"] = json::array();

  const auto types = TypeRegistry::Instance().Components();
  const auto dump_node = [&](kEntityId entity) {
    const GameObject object = scene.GetGameObject(entity);
    json node;
    node["path"] = object.GetPath();
    if (const uint64_t uid = scene.GetNodeUid(entity); uid != 0)
      node["uid"] = uid;
    node["active"] = object.IsActiveSelf();
    node["tag"] = object.GetTag();
    node["layer"] = object.GetLayer();

    json transform = json::object();
    if (const Transform* t = object.GetTransform()) {
      transform["position"] = Vec3(t->GetPosition());
      transform["rotation"] = Vec3(t->GetRotate());
      transform["scale"] = Vec3(t->GetScale());
      transform["local_position"] = Vec3(t->GetLocalPosition());
    }
    node["transform"] = transform;

    if (object.GetComponent<Rigidbody>()) {
      node["velocity"] = Vec3(scene.Physics().GetLinearVelocity(entity));
      node["angular_velocity"] =
          Vec3(scene.Physics().GetAngularVelocity(entity));
    }

    json components = json::object();
    for (const ComponentInfo* info : types) {
      if (!info->has || !info->has(scene, entity)) continue;
      json fields = json::object();
      for (const FieldInfo& field : info->fields) {
        mtext::Variant value;
        if (info->get_field &&
            info->get_field(scene, entity, field.name, &value))
          fields[field.name] = VariantToJson(value);
      }
      components[info->name] = std::move(fields);
    }
    node["components"] = std::move(components);
    result["nodes"].push_back(std::move(node));
  };

  if (nodes.empty()) {
    for (kEntityId entity : scene.GetRegistry().view<NodeInfo>())
      dump_node(entity);
  } else {
    std::set<kEntityId> dumped;
    for (const std::string& path : nodes) {
      const GameObject object = scene.Find(path);
      if (!object) {
        result["errors"].push_back("node not found: " + path);
        continue;
      }
      if (dumped.insert(object.GetEntity()).second)
        dump_node(object.GetEntity());
    }
  }
  return result.dump(2);
}

}  // namespace Mercury::introspection
