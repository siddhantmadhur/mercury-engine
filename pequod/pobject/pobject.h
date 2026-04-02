/**
 * pobject.h
 *
 * Object class which represents each object or entity drawn to the screen.
 * Rather than fixed properties (like OOP), properties are data stored inside
 * inside
 **/

#ifndef PEQUODENGINE_POBJECT_H
#define PEQUODENGINE_POBJECT_H
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <string>
#include "../properties/property.h"
#include <vector>
#include <ecs/ecs.hh>

#include "properties/position.h"

namespace Pequod {
    class PObject {
    public:
        PObject();
        ~PObject();

        uint64_t id = 0; // 0 signifies root
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
        std::shared_ptr<TProperty> Add(Args&&... args) {
            auto ptr = std::make_shared<TProperty>(args...);
            properties[typeid(TProperty)] = ptr;
            pending_registrations.push_back([ptr](std::shared_ptr<ECS> ecs, entity_id id) {
                ecs->AddProperty(id, ptr);
            });
            return ptr;
        }


        void SetECS(std::shared_ptr<ECS>);

    private:

        std::unordered_map<std::type_index, std::shared_ptr<Property>> properties;
        std::vector<std::function<void(std::shared_ptr<ECS>, entity_id)>> pending_registrations;
        std::shared_ptr<ECS> ecs;

    };
}

#endif //PEQUODENGINE_POBJECT_H