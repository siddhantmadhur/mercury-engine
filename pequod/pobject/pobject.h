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
        PObject(uint64_t id = 0);
        ~PObject();

        uint64_t id = 0; // 0 signifies root
        std::string name = "object";

        void AddChild(uint64_t child_id);
        std::vector<uint64_t> children;
    private:


        std::unordered_map<std::type_index, std::shared_ptr<Property>> properties;

    };
}

#endif //PEQUODENGINE_POBJECT_H