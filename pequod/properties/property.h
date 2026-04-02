//
// Created by Siddhant Madhur on 27/03/2026.
//

#ifndef PEQUODENGINE_PROPERTY_H
#define PEQUODENGINE_PROPERTY_H
#include <cstdint>

// Runs a function with all the types
// [CLAUDE] TODO: RUN_ON_PROPERTIES macro is defined but never invoked anywhere — remove or use
#define RUN_ON_PROPERTIES(func)     \
    func(Position);                 \
    func(Mesh);

namespace Pequod {
    enum class PropertyId {
        Mesh = 1,
        Position = 2,
    };

    class Property {
    public:
        Property();
    };
}
#endif //PEQUODENGINE_PROPERTY_H
