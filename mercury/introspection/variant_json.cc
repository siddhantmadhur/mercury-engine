// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "introspection/variant_json.h"

namespace Mercury::introspection {

nlohmann::json VariantToJson(const mtext::Variant& value) {
  using Type = mtext::Variant::Type;
  switch (value.GetType()) {
    case Type::kNil:
      return nullptr;
    case Type::kBool:
      return *value.Get<bool>();
    case Type::kInt:
      return *value.Get<int64_t>();
    case Type::kFloat:
      return *value.Get<double>();
    case Type::kString:
      return *value.Get<std::string>();
    case Type::kVector2: {
      const glm::vec2 v = *value.Get<glm::vec2>();
      return nlohmann::json::array({v.x, v.y});
    }
    case Type::kVector3: {
      const glm::vec3 v = *value.Get<glm::vec3>();
      return nlohmann::json::array({v.x, v.y, v.z});
    }
    case Type::kColor: {
      const glm::vec4 v = value.Get<mtext::Color>()->rgba;
      return nlohmann::json::array({v.r, v.g, v.b, v.a});
    }
    case Type::kNodePath:
      return nlohmann::json{{"path", value.Get<mtext::NodePath>()->path}};
    case Type::kExtResource:
      return nlohmann::json{{"ext", value.Get<mtext::ExtResource>()->id}};
    case Type::kResource:
      return nlohmann::json{{"res", value.Get<mtext::Resource>()->path}};
    case Type::kArray: {
      nlohmann::json array = nlohmann::json::array();
      for (const auto& item : *value.Get<mtext::Array>()) {
        array.push_back(VariantToJson(item));
      }
      return array;
    }
  }
  return nullptr;
}

}  // namespace Mercury::introspection
