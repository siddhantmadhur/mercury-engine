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
  std::shared_ptr<TProperty> Get() {
    auto it = properties.find(typeid(TProperty));
    if (it == properties.end()) return nullptr;
    return std::static_pointer_cast<TProperty>(it->second);
  }

  template <std::derived_from<Property> TProperty, class... Args>
  std::shared_ptr<TProperty> Add(Args &&...args) {
    auto ptr = std::make_shared<TProperty>(args...);
    properties[typeid(TProperty)] = ptr;
    return ptr;
  }

 private:
  /**
   * Not very memory optimized and slow for scenes with a number of objects but
   * the goal is to make it more streamlined later on. Like if an ECS exist the
   * pointer would simply point to the index in that property array.
   */
  std::unordered_map<std::type_index, std::shared_ptr<Property> > properties;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_POBJECT_H
