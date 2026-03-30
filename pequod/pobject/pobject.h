/**
 * pobject.h
 *
 * Object class which represents each object or entity drawn to the screen.
 * Rather than fixed properties (like OOP), properties are data stored inside
 * inside
 **/

#ifndef PEQUODENGINE_POBJECT_H
#define PEQUODENGINE_POBJECT_H
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <string>
#include "property.h"
#include <vector>

namespace Pequod {
    class PObject {
    public:
        PObject();
        ~PObject();

        uint64_t id = -1; // -1 means it is not registered to a scene
        std::string name = "object";


        //template <class TProperty>
        //std::shared_ptr<TProperty> GetProperty();

        //template <class TProperty>
        //void AddProperty(std::shared_ptr<TProperty>);

        void AddChild(uint64_t child_id);

    private:


        std::vector<uint64_t> children;
        std::unordered_map<std::type_index, std::shared_ptr<Property>> properties;

    };
}

#endif //PEQUODENGINE_POBJECT_H