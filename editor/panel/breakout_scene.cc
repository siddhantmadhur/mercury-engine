#include "breakout_scene.hh"
#include "Jolt/Physics/Body/AllowedDOFs.h"
#include "engine/world_scene.h"
#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"
#include "rigidbody/box2d.hh"
#include "rigidbody/rigidbody.hh"
#include <format>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gameobjects/gameobject.hh>
#include <gameobjects/quad.hh>
#include <imgui/imgui.h>
#include <iostream>

#define ZOOM 4.0f
    
#define height_s (sapp_heightf() * (1.0f / ZOOM))
#define width_s (sapp_widthf() * (1.0f / ZOOM))

#define playerPos ecs.getPosition(player)->position 
#define playerSize ecs.getMesh(player)->scale

class BallRigidBody : public Box2D {
public:
    BallRigidBody(glm::vec3 pos, glm::vec3 size) : Box2D(pos, size) { PDebug::log("initialized ball rigidbody"); }
    void OnCollisionEnter(entity_id) override { collide_with_ball = true; };
    void OnCollision(entity_id id) override { collided_entity = id; };
    void OnCollisionLeave(entity_id) override { collide_with_ball = false; };
    bool collide_with_ball = false;
    entity_id collided_entity = -1;
};
class PlayerRigidBody : public Box2D {
public:
    PlayerRigidBody(glm::vec3 pos, glm::vec3 size) : Box2D(pos, size) { PDebug::log("initialized player rigidbody"); }
    void OnCollisionEnter(entity_id) override {};
    void OnCollision(entity_id) override {};
    void OnCollisionLeave(entity_id) override {};
};


static std::vector<entity_id> bricks_to_remove;



class WallRigidBody : public Box2D {
public:
    WallRigidBody(glm::vec2 pos, glm::vec2 size) : Box2D(pos, size) { PDebug::log("initialized wall rigidbody"); }
    void OnCollisionEnter(entity_id) override {};
    void OnCollision(entity_id) override {};
    void OnCollisionLeave(entity_id) override { };

};
class BrickRigidBody : public Box2D {
public:
    BrickRigidBody(glm::vec2 pos, glm::vec2 size) : Box2D(pos, size) { PDebug::log("initialized brick rigidbody"); }
    void OnCollisionEnter(entity_id) override {
        PDebug::log(std::format("Brick ID: {}", this->id));
        bricks_to_remove.push_back(id);
    };
    void OnCollision(entity_id) override {};
    void OnCollisionLeave(entity_id) override { };

};

void BreakoutScene::OnStart() {

    srand(time(NULL));


    SetBgColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    {
        auto bg = ecs.createEntity();
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 size = glm::vec3(sapp_widthf(), sapp_heightf(), 0.001f);
        Quad *quad = new Quad(pos, size, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        ecs.addPosition(player, quad->position);
        ecs.addMesh(player, quad->mesh);
        
        //PlayerRigidBody* rigid_body = new PlayerRigidBody(pos, size);
        //rigid_body->allowed_dofs = JPH::EAllowedDOFs::TranslationX;

        //ecs.addRigidBody(player, rigid_body);
        //ecs.SetMotionType(player, JPH::EMotionType::Dynamic);
        
    }

    { // CAMERA

        // TODO: add createOrtho and createPerspective in the engine (maybe to scene?) itself to reduce this code as well
        Camera playerCam = Camera(width_s / height_s);
        playerCam.configure2D(sapp_widthf(), sapp_heightf(), ZOOM);
        SetPlayerCamera(playerCam);
    }

    { // left wall
        entity_id wall = ecs.createEntity();
        glm::vec2 size = glm::vec2(10.0f, height_s);
        glm::vec2 pos = glm::vec2(-size.x / 2.0f, size.y / 2.0f);

        Quad* quad = new Quad(pos, size, glm::vec4(1.0f));
        ecs.addPosition(wall, quad->position);
        ecs.addMesh(wall, quad->mesh);

        WallRigidBody* rigid_body = new WallRigidBody(pos, size);
        ecs.addRigidBody(wall, rigid_body);
        ecs.SetRestitution(wall, 0.0f);
        ecs.SetMotionType(wall, JPH::EMotionType::Static);
    }

    { // right wall
        entity_id wall = ecs.createEntity();
        glm::vec2 size = glm::vec2(10.0f, height_s);
        glm::vec2 pos = glm::vec2(width_s + (size.x / 2.0f), size.y / 2.0f);

        Quad* quad = new Quad(pos, size, glm::vec4(1.0f));
        ecs.addPosition(wall, quad->position);
        ecs.addMesh(wall, quad->mesh);

        WallRigidBody* rigid_body = new WallRigidBody(pos, size);
        ecs.addRigidBody(wall, rigid_body);
        ecs.SetRestitution(wall, 0.0f);
        ecs.SetMotionType(wall, JPH::EMotionType::Static);
    }
    
    { // upper wall
        entity_id wall = ecs.createEntity();
        glm::vec2 size = glm::vec2(width_s, 10.0f);
        glm::vec2 pos = glm::vec2(width_s / 2.0f, height_s + (size.y / 2.0f));

        Quad* quad = new Quad(pos, size, glm::vec4(1.0f));
        ecs.addPosition(wall, quad->position);
        ecs.addMesh(wall, quad->mesh);

        WallRigidBody* rigid_body = new WallRigidBody(pos, size);
        ecs.addRigidBody(wall, rigid_body);
        ecs.SetRestitution(wall, 0.0f);
        ecs.SetMotionType(wall, JPH::EMotionType::Static);
    }

    { // PLAYER
        player = ecs.createEntity();
        glm::vec3 pos = glm::vec3(width_s / 2.0f, 16, 0.0f);
        glm::vec3 size = glm::vec3(24.0f, 3.0f, 1.0f);
        Quad *quad = new Quad(pos, size, glm::vec4(1.0f));
        ecs.addPosition(player, quad->position);
        ecs.addMesh(player, quad->mesh);
        
        PlayerRigidBody* rigid_body = new PlayerRigidBody(pos, size);
        rigid_body->allowed_dofs = JPH::EAllowedDOFs::TranslationX;

        ecs.addRigidBody(player, rigid_body);
        ecs.SetMotionType(player, JPH::EMotionType::Dynamic);
    }

    { // ball
        ball_dx = glm::vec2(0.0f);

        ball = ecs.createEntity();
        glm::vec3 size = glm::vec3(4.0f, 4.0f, 1.0f);
        glm::vec3 pos = glm::vec3(playerPos.x, playerPos.y + playerSize.y, 0.0f);
        Quad* quad = new Quad(pos, size, glm::vec4(1.0f));
        ecs.addPosition(ball, quad->position);
        ecs.addMesh(ball, quad->mesh);
        
        BallRigidBody* rigid_body = new BallRigidBody(pos, size);
        ecs.addRigidBody(ball, rigid_body);
        ecs.SetFriction(ball, 0.0f);
        ecs.SetRestitution(ball, 1.0f);
        ecs.SetMotionType(ball, JPH::EMotionType::Dynamic);
    }

    { // BRICKS 
        float padding_around = 8.0f;
        float total_width = width_s - (padding_around * 2)  - (20);
        glm::vec2 brick_size = glm::vec2(width_s / 10, 4.0f);
        glm::vec2 offset = glm::vec2(padding_around + (brick_size.x), height_s - brick_size.y - 20);

        for (int j = 0; j < 8; j++) {
            offset.x = padding_around + (brick_size.x);
            for (int i = 0; i < 8; i++) {
                auto brick_id = ecs.createEntity();
                bricks.push_back(brick_id);
                Quad* quad = new Quad(offset, brick_size, glm::vec4(1.0 / (8.0/j), 0.1f, 0.8f, 1.0f));
                ecs.addPosition(brick_id, quad->position);
                ecs.addMesh(brick_id, quad->mesh);
               
                BrickRigidBody* rigid_body = new BrickRigidBody(offset, brick_size);

                ecs.addRigidBody(brick_id, rigid_body);
                ecs.SetMotionType(brick_id, JPH::EMotionType::Static);


                offset.x += brick_size.x;
                offset.x += 2;
            }
            offset.y -= brick_size.y ;
            offset.y -= 2;
        }
    }
   
}

#define CONSTANT_VELOCITY false

void BreakoutScene::OnTickUpdate(float tick_t) {

    { // MOVEMNT

        while (bricks_to_remove.size() > 0) {
            entity_id brick_id = bricks_to_remove.back();
            
            ecs.Disable(brick_id);

            bricks_to_remove.pop_back();

        }

        if ((!game_started) && IsKeyPressed(SAPP_KEYCODE_UP)) {
            float deg = 30 + (rand() % 120);
            ball_dx = glm::vec2(glm::cos(glm::radians(deg)), glm::sin(glm::radians(deg)));
            game_started = true;
#if !CONSTANT_VELOCITY
            ecs.setVelocity(ball, glm::vec3(ball_dx.x, ball_dx.y, 0.0f) * ball_speed * tick_t);
#endif
        }

        if (game_started) {
            glm::vec3 & pos = ecs.getPosition(ball)->raw_position;
            if (pos.y < 0) {
                game_started = false;
                pos = playerPos;
                pos.y += playerSize.y / 2.0f;
            }
        }

        float direction = 0;
        if (IsKeyPressed(SAPP_KEYCODE_LEFT)) {
            direction -= 1.0f;
        }
        if (IsKeyPressed(SAPP_KEYCODE_RIGHT)) {
            direction += 1.0f;
        }

        if (IsKeyPressed(SAPP_KEYCODE_LEFT_SHIFT) || IsKeyPressed(SAPP_KEYCODE_RIGHT_SHIFT)) {
            direction = direction / 4.0f;
        }
        if (IsKeyPressed(SAPP_KEYCODE_RIGHT_CONTROL)) {
            direction = direction * 2.0f;
        }

        ecs.setVelocity(player, glm::vec3(direction * player_speed * tick_t, 0.0f, 0.0f));

        /**

        if (playerPos.x - (playerSize.x / 2.0f) < 0) {
            glm::vec3 newPos = playerPos;
            newPos.x = playerSize.x / 2.0f;
            ecs.setVelocity(player, glm::vec3(0.0f));
            ecs.SetPosition(player, newPos);
        } 
        if (playerPos.x + (playerSize.x / 2.0f) > width_s) {
            glm::vec3 newPos = playerPos;
            newPos.x = width_s - (playerSize.x / 2.0f);
            ecs.setVelocity(player, glm::vec3(0.0f));
            ecs.SetPosition(player, newPos);
        } 
        **/
        
        /**
        if (player) {
            player->Move(glm::vec3(direction *  player_speed, 0.0f, 0.0f));
            if (direction < 0) { // going left
                glm::vec3 pos = playerPos;
                pos.x = glm::max(playerSize.x + 2, pos.x);
                player->SetPosition(pos);
            }
            if (direction > 0) { // going left
                glm::vec3 pos = playerPos;
                pos.x = glm::min((width / 2.0f) - playerSize.x - 2, pos.x);
                player->SetPosition(pos);
            }
        }

        if (game_started) {
            ball->Move(glm::vec3(ball_dx.x, ball_dx.y, 0.0f) *  ball_speed);
        } else {
            ball->SetPosition(glm::vec3(playerPos.x, playerPos.y + playerSize.y, 0.0f));
        }
        **/
    
        if (!game_started) {
            //ecs.setVelocity(ball, glm::vec3(ball_dx.x, ball_dx.y, 0.0f) * ball_speed);
            glm::vec3 newPos = playerPos;
            newPos.y += playerSize.y;
            glm::vec3 & ballPos = ecs.getPosition(ball)->raw_position;
            ecs.SetPosition(ball, newPos);
        } else {
#if CONSTANT_VELOCITY

            ecs.setVelocity(ball, glm::vec3(ball_dx.x, ball_dx.y, 0.0f) * ball_speed * tick_t);
#else
            glm::vec3 ball_velocity = ecs.GetVelocity(ball);
            ball_velocity = glm::normalize(ball_velocity);
            ecs.setVelocity(ball, ball_velocity * ball_speed * tick_t);
#endif
        }

    }



    /**
    auto ballPos = ball->GetPosition();
    if (ballPos.x < 0 || ballPos.x > width_s) {
        ball_dx.x *= -1;
    }
    if (ballPos.y > height_s) {
        ball_dx.y *= -1;
    }
    if (ballPos.y < 0) {
        game_started = 0;
    }

    if (doCollide(ball, player)) {
        ball_dx.y *= -1;
    }
    for (int i = 0; i < bricks.size(); i++) {
        if (!bricks[i]->disable && doCollide(ball, bricks[i])) {
            bricks[i]->disable = true;
            
            auto A_half = ball->GetSize() / 2.0f;
            auto B_half = player->GetSize() / 2.0f;

            auto overlapX = (A_half.x + B_half.x) - glm::abs(ball->GetPosition().x - bricks[i]->GetPosition().x);
            auto overlapY = (A_half.y + B_half.y) - glm::abs(ball->GetPosition().y - bricks[i]->GetPosition().y);
            //ball->Move(glm::vec3(overlapX, overlapY, 0.0f) * -1.0f);

            if (ball_dx.x < ball_dx.y) {
                ball_dx.y *= -1;
            } else {
                ball_dx.x *= -1;
            }

            //std::cout << "Collide with [" << i << "]"  << " @ (" << diff.x << ", " << diff.y << ")"  << std::endl; 
            //std::cout << "Collide with [" << i << "]"  << " @ (" << overlapX << ", " << overlapY << ")"  << std::endl; 
        }
    }
**/ 
}




void BreakoutScene::OnFrameUpdate() {


    ImGui::Begin("pos", NULL, 0);
   
    glm::vec3 ballPos = ecs.getPosition(ball)->raw_position;
    ImGui::Text("player: (%.2f, %.2f, %.2f)", ballPos.x, ballPos.y, ballPos.z);
    //ImGui::Text("ball: (%.2f, %.2f)", ball->GetPosition().x, ball->GetPosition().y);

    ImGui::SliderFloat("Player Sp.", &player_speed, 0.0f, 100.0f);
    ImGui::SliderFloat("Ball Sp.", &ball_speed, 0.0f, 100.0f);

    ImGui::End();

}
void BreakoutScene::OnDestroy() {
}

void BreakoutScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}
