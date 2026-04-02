#include <properties/position.h>


namespace Pequod {
    Position::Position() {
    }

    void Position::Set(glm::vec3 new_pos) {
        this->position = new_pos;
        this->interpolated_position = new_pos;
    }

    glm::vec3 Position::Get() {
        return this->position;
    }
}
