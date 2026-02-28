
#include "scene.hh"
#include <engine/scene.hh>
#include <gameobjects/quad.hh>

#include <imgui/imgui.h>

#include <iostream>

#define ZOOM 4.0f
#define SCALE (1.0f/ZOOM) 

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// MAKE THIS A PART OF THE SCENE
float width = 1280.0f;
float height = 720.0f;

#define TOP_LEFT glm::vec2((-width/2.0f)*SCALE, (height/2.0f)*SCALE)
#define TOP_RIGHT glm::vec2((width/2.0f)*SCALE, (height/2.0f)*SCALE)
#define BOTTOM_LEFT glm::vec2((-width/2.0f)*SCALE, -((height/2.0f)*SCALE))
#define BOTTOM_RIGHT glm::vec2((width/2.0f)*SCALE, -((height/2.0f)*SCALE))

void PongScene::OnStart() {
    
    ImGui::LoadIniSettingsFromDisk("position_settings.ini");
    { // configure 2d camera


        // TODO: add createOrtho and createPerspective in the engine (maybe to scene?) itself to reduce this code as well


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
    
  { // configure paddles here
        glm::vec2 offset_from_origin = glm::vec2(width / 2.0f, height / 2.0f) * (1/ZOOM);
        Shapes::Quad *enemy = new Shapes::Quad(glm::vec2(2.0f, 8.0f), glm::vec2((+offset_from_origin.x) - 16, 0.0f), glm::vec4(1.0f));
        //Shapes::Quad player = Shapes::Quad();
        enemy->UseTexture(false);

        AddObject(*enemy);

        this->enemy = enemy;
    }
  { // configure paddles here
        //glm::vec2 offset_from_origin = glm::vec2(width / 2.0f, height / 2.0f) * (1/ZOOM);
        //Shapes::Quad *ball = new Shapes::Quad(glm::vec2(2.0f), glm::vec2((-offset_from_origin.x) + 16, 10.0f), glm::vec4(1.0f));
        Shapes::Quad *ball = new Shapes::Quad(glm::vec2(2.0f), glm::vec2(0.0f), glm::vec4(1.0f));
        //Shapes::Quad player = Shapes::Quad();
        //ball->UseTexture(true);

        AddObject(*ball);

        this->ball = ball;
    }
}

bool doCollide(glm::vec2 posA, glm::vec2 sizeA, glm::vec2 posB, glm::vec2 sizeB) {
  bool collisionX = posA.x + sizeA.x >= posB.x && posB.x + sizeB.x >= posA.x;
  bool collisionY = posA.y + sizeA.y >= posB.y && posB.y + sizeB.y >= posA.y;
  return collisionX && collisionY;
}

void PongScene::OnUpdate() {
    #define SPEED 0.3f
    if (this->player != NULL) {
        #define COLLISION_SIZE 100.0f
        float pos = player->GetPosition().y;
        float size = player->GetSize().y;


        float vel = direction.y * delta_t * SPEED;

        if (vel > 0) {
            float newPos = pos + vel;

            float finPos = glm::min(TOP_LEFT.y - size, newPos);
            
            player->SetPosition(glm::vec3(player->GetPosition().x, finPos, 0));
        } else if (vel < 0) {
            float newPos = pos + vel;

            float finPos = glm::max(BOTTOM_LEFT.y + size, newPos);
            
            player->SetPosition(glm::vec3(player->GetPosition().x, finPos, 0));

        }
        /**
        glm::vec3 velocity = glm::vec3(direction.x * delta_t * SPEED, velocity_y, 0.0f);
        if ((!doCollide(player->GetPosition(), player->GetSize(), TOP_LEFT, glm::vec2(TOP_RIGHT.x, 100.0f))) && velocity.y > 0) { // player colliding above
            this->player->Move(velocity);
        }
        else if ((!doCollide(player->GetPosition(), player->GetSize(), BOTTOM_LEFT - glm::vec2(0.0f, COLLISION_SIZE), glm::vec2(BOTTOM_RIGHT.x, COLLISION_SIZE))) && velocity.y < 0) { // player colliding below
            this->player->Move(velocity);
        }
        **/
        glm::vec3 position = this->player->GetPosition();
        /**
        #define MAX_CEIL 70 
        if (position.y > MAX_CEIL) {
            position.y = MAX_CEIL;
        }
        if (position.y < -MAX_CEIL) {
            position.y = -MAX_CEIL;
        }
        this->player->SetPosition(position);
        //std::cout << "update value: (" << position.x << ", " << position.y << ")" << std::endl;
        **/
    }

    glm::vec2 playerPos = player->GetPosition();

    { // HANDLE ENTITY COLLISION HERE
      if (doCollide(player->GetPosition(), player->GetSize(), ball->GetPosition(), ball->GetSize())) {
            std::cout << "COLLIDE!" << std::endl;
        }
    }

    bool open = true;

    ImGui::Begin("positions", &open, 0);
    glm::vec2 posA = player->GetPosition();
    glm::vec2 sizeA = player->GetSize();
    ImGui::Text("PLAYER: (%.2f, %.2f) (%.2f, %.2f)", posA.x, posA.y, sizeA.x, sizeA.y);
    glm::vec2 posB = enemy->GetPosition();
    glm::vec2 sizeB = enemy->GetSize();
    ImGui::Text("ENEMY: (%.2f, %.2f) (%.2f, %.2f)", posB.x, posB.y, sizeB.x, sizeB.y);
    glm::vec2 posC = ball->GetPosition();
    glm::vec2 sizeC = ball->GetSize();
    ImGui::Text("BALL: (%.2f, %.2f) (%.2f, %.2f)", posC.x, posC.y, sizeC.x, sizeC.y);

    ImGui::Text("UPPER BOUND: (%.2f, %.2f) -> (%.2f, %.2f)", TOP_LEFT.x, TOP_LEFT.y, TOP_RIGHT.x, TOP_RIGHT.y);
    ImGui::End();

}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && event->key_repeat == false) {
        ImGui::SaveIniSettingsToDisk("position_settings.ini");
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
        ImGui::SaveIniSettingsToDisk("position_settings.ini");
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

