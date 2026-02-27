#ifndef MERCURY_QUAD_IMPL_HH_
#define MERCURY_QUAD_IMPL_HH_

#include <gameobjects/gameobject.hh>
#include <glm/glm.hpp>

namespace Shapes {

class Quad : public GameObject {
public:
    Quad(glm::vec2 size=glm::vec2(1.0f), glm::vec2 position = glm::vec2(0.0f), glm::vec4 color = glm::vec4(1.0f));
private:
    glm::vec2 size;
    glm::vec2 position; 
    glm::vec4 color;
};

}

#endif
