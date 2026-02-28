
#include "scene.hh"
#include "glm/trigonometric.hpp"
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

void ResetRound(Shapes::Quad* ball, glm::vec2& vector, int playerWon) {
    float deg = rand() % 60;
    deg -= 30;
    std::cout << "Using degree: " << deg << std::endl;
    vector = glm::vec2(glm::cos(glm::radians(deg)), glm::sin(glm::radians(deg)));
    
    if (playerWon) {
        vector.x *= -1;
    }
    ball->SetPosition(glm::vec3(0.0f));
    //vector = glm::normalize(vector);
    #define MAX_ANGLE 0.80f
    

}

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

    { // configure ball here
        //glm::vec2 offset_from_origin = glm::vec2(width / 2.0f, height / 2.0f) * (1/ZOOM);
        //Shapes::Quad *ball = new Shapes::Quad(glm::vec2(2.0f), glm::vec2((-offset_from_origin.x) + 16, 10.0f), glm::vec4(1.0f));
        Shapes::Quad *ball = new Shapes::Quad(glm::vec2(2.0f), glm::vec2(0.0f), glm::vec4(1.0f));
        //Shapes::Quad player = Shapes::Quad();
        ball->UseTexture(false);

        AddObject(*ball);

        this->ball = ball;

        srand(time(NULL));

        ResetRound(ball, ballVector, 0);
    }
}

bool doCollide(Shapes::Quad* A, glm::vec2 posB, glm::vec2 sizeB) { // (still object, moving object)
    glm::vec2 sizeA = A->GetSize();
    glm::vec2 posA = A->GetPosition();
    posA.x -= sizeA.x / 2.0f;
    posA.y -= sizeA.y / 2.0f;

    bool collisionX = posA.x + sizeA.x >= posB.x && posB.x + sizeB.x >= posA.x;
    bool collisionY = posA.y + sizeA.y >= posB.y && posB.y + sizeB.y >= posA.y;

    return (collisionX && collisionY);
}

bool doCollide(Shapes::Quad* A, Shapes::Quad* B) { // (still object, moving object)
    glm::vec2 sizeA = A->GetSize();
    glm::vec2 posA = A->GetPosition();
    posA.x -= sizeA.x / 2.0f;
    posA.y -= sizeA.y / 2.0f;

    glm::vec2 sizeB = B->GetSize();
    glm::vec2 posB = B->GetPosition();
    posB.x -= sizeB.x / 2.0f;
    posB.y -= sizeB.y / 2.0f;

    bool collisionX = posA.x + sizeA.x >= posB.x && posB.x + sizeB.x >= posA.x;
    bool collisionY = posA.y + sizeA.y >= posB.y && posB.y + sizeB.y >= posA.y;

    return (collisionX && collisionY);
}


void PongScene::OnUpdate() {
#define enemyPos enemy->GetPosition()
#define enemySize enemy->GetSize()
#define ballPos ball->GetPosition()
   
    direction.y = 0;
    if (IsKeyPressed(SAPP_KEYCODE_W)) {
        direction.y = 1.0f;
    }
    if (IsKeyPressed(SAPP_KEYCODE_S)) {
        direction.y = -1.0f;
    }
    enemyDx = 0.0f;
    if (IsKeyPressed(SAPP_KEYCODE_UP)) {
        enemyDx = 1.0f;
    }
    if (IsKeyPressed(SAPP_KEYCODE_DOWN)) {
        enemyDx = -1.0f;
    }
   
    if (doCollide(player, ball)) {
        std::cout << "COLLISION" << std::endl;
    }

    { // HANDLE ENTITY COLLISION: PLAYER
        if (doCollide(player, ball) && ballVector.x < 0) {
            ballVector.x *= -1;
        }
    }

    { // HANDLE ENTITY COLLISION: ENEMY
        if (doCollide(enemy, ball) && ballVector.x > 0) {
            ballVector.x *= -1;
        }
    }

    {
        #define SPEED 0.15

        if (this->enemy != NULL) {
            #define COLLISION_SIZE 100.0f

            float pos = enemy->GetPosition().y;
            float size = enemy->GetSize().y;
            float vel = enemyDx * delta_t * SPEED;
            if (vel > 0) {
                float newPos = pos + vel;
                float finPos = glm::min(TOP_LEFT.y - size, newPos);
                enemy->SetPosition(glm::vec3(enemy->GetPosition().x, finPos, 0));
            } else if (vel < 0) {
                float newPos = pos + vel;
                float finPos = glm::max(BOTTOM_LEFT.y + size, newPos);
                enemy->SetPosition(glm::vec3(enemy->GetPosition().x, finPos, 0));
            }
        }
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
        }

        // movements after all the prev calculations
        #define BALL_SPEED 0.10f
        #define ENEMY_SPEED 0.04f
        #define ENEMY_STEPS 0.3f

        ball->Move(glm::vec3(ballVector.x, ballVector.y, 0.0f) * delta_t * BALL_SPEED);

        #define AI false

        #if AI
        if (1) {
            float diff = ballPos.y - enemyPos.y;
           
            if (diff < enemyDx) {
                enemyDx -= ENEMY_STEPS * delta_t * ENEMY_SPEED;
            } else if (diff > enemyDx) {
                enemyDx += ENEMY_STEPS * delta_t * ENEMY_SPEED;
            }

            enemy->Move(glm::vec3(0.0f, enemyDx, 0.0f));

        }
#endif



    }

    { // MOVE BALL HERE
        #define COLLISION_SIZE 100.0f

        //glm::vec2 vel = direction * delta_t * SPEED;

            glm::vec3 pos = ball->GetPosition();
            glm::vec2 size = ball->GetSize();

        
        { // Handle bouncing against floor and ceil 
        

            if (doCollide(ball, TOP_LEFT, glm::vec2(width, COLLISION_SIZE)) && ballVector.y > 0) {
                ballVector.y *= -1;
            }
            if (doCollide(ball, BOTTOM_LEFT - glm::vec2(0, COLLISION_SIZE), glm::vec2(width, COLLISION_SIZE)) && ballVector.y < 0) {
                ballVector.y *= -1;
            }
        }

        { // Handle scores
            if ((pos.x - (size.x / 2.0f)) < TOP_LEFT.x) {
                std::cout << "score for enemy" << std::endl;
                enemyPoints += 1;
                ResetRound(ball, ballVector, 0);
            } 
            if (pos.x + (size.x / 2.0f) > TOP_RIGHT.x) {
                std::cout << "score for player" << std::endl;
                playerPoints += 1;
                ResetRound(ball, ballVector, 1);
            } 
        }
    }

    {
        ImGui::Begin("Scores", NULL, 0);
        ImGui::Text("PLAYER ONE - %d", playerPoints);
        ImGui::Text("PLAYER TWO - %d", enemyPoints);
        ImGui::End();
    }


    if (false){
        ImGui::Begin("positions", NULL, 0);
        glm::vec2 posA = player->GetPosition();
        glm::vec2 sizeA = player->GetSize();
        ImGui::Text("PLAYER: (%.2f, %.2f) (%.2f, %.2f)", posA.x, posA.y, sizeA.x, sizeA.y);
        glm::vec2 posB = enemy->GetPosition();
        glm::vec2 sizeB = enemy->GetSize();
        ImGui::Text("ENEMY: (%.2f, %.2f) (%.2f, %.2f)", posB.x, posB.y, sizeB.x, sizeB.y);
        glm::vec2 posC = ball->GetPosition();
        glm::vec2 sizeC = ball->GetSize();
        ImGui::Text("BALL: (%.2f, %.2f) (%.2f, %.2f)", posC.x, posC.y, sizeC.x, sizeC.y);

        //ImGui::Text("BALL DIRECTION: (%.2f, %.2f) -> (%.2f, %.2f)", TOP_LEFT.x, TOP_LEFT.y, TOP_RIGHT.x, TOP_RIGHT.y);
        ImGui::Text("BALL DIRECTION: (%.2f, %.2f)", ballVector.x, ballVector.y);
        ImGui::End();
    }

    ticks += 1;
}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN && event->key_repeat == false) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}


void PongScene::OnEnd() {
    std::cout << "Pong closing..."  << std::endl;
    ImGui::SaveIniSettingsToDisk("position_settings.ini");
}
