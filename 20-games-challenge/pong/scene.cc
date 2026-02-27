
#include "scene.hh"
#include <engine/scene.hh>
#include <gameobjects/quad.hh>

#include <iostream>

#define ZOOM 4.0f

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// MAKE THIS A PART OF THE SCENE
float width = 1280.0f;
float height = 720.0f;

void PongScene::OnStart() {
    
    { // configure 2d camera
        Camera playerCam = Camera(16.0/9.0);
        glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
        playerCam.setPosition(pos);
        
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, pos.y, 0.0f));
        float zoom = ZOOM;
        view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));


        glm::mat4 proj = glm::ortho<float>(-(width / 2.0f), width / 2.0f , -(height / 2.0f), (height / 2.0f), -1.0f, 1.0f);

        playerCam.setProj(proj);
        playerCam.setView(view);

        SetPlayerCamera(playerCam);
    }

    {
        SetBgColor(glm::vec4(0.0f));
    }


    { // configure paddles here
        glm::vec2 offset_from_origin = glm::vec2(width / 2.0f, height / 2.0f) * (1/ZOOM);
        Shapes::Quad *player = new Shapes::Quad(glm::vec2(2.0f, 8.0f), glm::vec2((-offset_from_origin.x) + 16, 0.0f), glm::vec4(1.0f));
        //Shapes::Quad player = Shapes::Quad();
        player->UseTexture(false);

        AddObject(*player);

        this->player = player;
    }
}

void PongScene::OnUpdate() {
    #define SPEED 0.3f
    if (this->player != NULL) {
        this->player->Move(glm::vec3(direction.x * delta_t * SPEED, direction.y * delta_t * SPEED, 0.0f));
        glm::vec3 position = this->player->GetPosition();
        #define MAX_CEIL 70 
        if (position.y > MAX_CEIL) {
            position.y = MAX_CEIL;
        }
        if (position.y < -MAX_CEIL) {
            position.y = -MAX_CEIL;
        }
        this->player->SetPosition(position);
        //std::cout << "update value: (" << position.x << ", " << position.y << ")" << std::endl;
    }
}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && event->key_repeat == false) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }

        if (event->key_code == SAPP_KEYCODE_W) {
            direction.y += 1.0f;
        }
        if (event->key_code == SAPP_KEYCODE_S) {
            direction.y -= 1.0f;
        }
    }
    if (event->type == SAPP_EVENTTYPE_KEY_UP) {
        if (event->key_code == SAPP_KEYCODE_W) {
            direction.y -= 1.0f;
        } 
        if (event->key_code == SAPP_KEYCODE_S) {
            direction.y += 1.0f;
        }
    }


   
    if (this->player) {
        //std::cout << "vertex size: " << player->vertices.size() << std::endl;
        //this->player->Move(glm::vec3(direction.x * delta_t * SPEED, direction.y * delta_t * SPEED, 0.0f));
    }
}

