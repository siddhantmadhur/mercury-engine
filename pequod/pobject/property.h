//
// Created by Siddhant Madhur on 27/03/2026.
//

#ifndef PEQUODENGINE_PROPERTY_H
#define PEQUODENGINE_PROPERTY_H
#include <cstdint>

namespace Pequod {
    enum class PropertyId {
        Mesh = 1,
        Position = 2,
    };
    class Property {
    public:
        Property(PropertyId property_id);
    };
}
#endif //PEQUODENGINE_PROPERTY_H