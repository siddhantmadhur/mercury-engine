/**
* position.hh 
* 
* A physics based component for the ECS
* The way it is supposed to work is that there is a 
* raw_position that is set per tick
* 
* The issue with that is that since theres only 20
* ticks in a second, the game effectively looks like
* 20fps if you render the position
*
* So the position value is interpolated to play
* between those ticks and thus looks smooth
**/


#ifndef PEQUOD_POSITION_IMPL_HH_
#define PEQUOD_POSITION_IMPL_HH_

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <sokol/sokol_gfx.h>
#include <shaders/generic_texture.glsl.hh>

// TODO: allow two gameobjects to combine their vertices so that the no. of draw calls is reduced
class Position {
public:
    Position(glm::vec3 position = glm::vec3(0.0f));
    glm::vec3 raw_position; // actual position of object interpolated per frame to smooth animation out
    glm::vec3 position; // update this per tick
    
private:
    //glm::vec3 future_position; // position needed to be updated to next frame/tick 
};

#endif
