/**
 * pobject.h
 *
 * Object class which represents each object or entity drawn to the screen.
 * Rather than fixed properties (like OOP), properties are data stored inside
 * Look at DOD to better understand this.
 *
 **/

#ifndef PEQUODENGINE_POBJECT_H
#define PEQUODENGINE_POBJECT_H
#include <tinyxml2.h>

#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../properties/property.h"
#include "properties/collision_body.h"
#include "properties/mesh.h"
#include "properties/texture2d.h"
#include "properties/transform.h"

namespace Pequod {

/**
 * @brief Object class which represents every entity in the game engine
 *
 * It is a generic class that can have properties dynamically added rather than
 * defined at compile time. This is kind of stupid to do but the reason I
 * designed it this way was so that later on if I ever need to switch to use an
 * ECS or even think of memory optimizations, the user code would not have to
 * change because of it. Basically prioritized compatibility over optimizations
 * for now which is fine.
 */
class PObject {
 public:
  PObject();
  ~PObject();

  uint64_t id = 0;  // 0 signifies root
  std::string name = "object";

  void AddChild(uint64_t child_id);

  std::vector<uint64_t> children = {};

  template <std::derived_from<Property> TProperty>
  TProperty* Get() {
    auto property = std::get<TProperty*>(properties_);
    return property;
  }

  template <std::derived_from<Property> TProperty, class... Args>
  TProperty* Add(Args&&... args) {
    auto property = new TProperty(args...);
    std::get<TProperty*>(properties_) = property;
    return property;
  }

 private:
  std::tuple<Transform*, Mesh*, Texture2D*> properties_;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_POBJECT_H
