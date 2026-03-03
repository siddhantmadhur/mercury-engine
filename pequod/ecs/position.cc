#include <ecs/position.hh>


Position::Position(glm::vec3 init_position) {
    this->raw_position = init_position;
    this->position = init_position;
}
