#ifndef MERCURY_QUAD_IMPL_HH_
#define MERCURY_QUAD_IMPL_HH_

#include <glm/glm.hpp>
#include <gameobjects/gameobject.hh>



namespace Pequod {
    class Quad : public GameObject {
    public:
        Quad(glm::vec2 position=glm::vec2(0.0f), glm::vec2 size=glm::vec2(2.0f), glm::vec4 color=glm::vec4(1.0f));
    protected:
    };
}

#endif
