#include "scene.hh"
#include "Jolt/Math/Quat.h"
#include "Jolt/Math/Real.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/NarrowPhaseQuery.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/EActivation.h"
#include "ecs/entity.hh"
#include "engine/scene.hh"
#include "gameobjects/camera.hh"
#include "gameobjects/quad.hh"
#include "glm/fwd.hpp"
#include "rigidbody/box2d.hh"
#include "rigidbody/rigidbody.hh"
#include <format>
#include <iostream>
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include <debugger/debugger.hh>


#define ZOOM 8.0f
    
#define heights (sapp_heightf() * (1 / ZOOM))
#define widths (sapp_widthf() * (1 / ZOOM))


class PlayerPaddle : public Box2D {
public:
    PlayerPaddle(glm::vec2 position, glm::vec2 size) : Box2D(position, size) {}
    void OnCollisionEnter(entity_id) {
    }
    void OnCollision(entity_id) {
    }
    void OnCollisionLeave(entity_id) {
    }
};

class HorizontalBound : public Box2D {
public:
    HorizontalBound(glm::vec2 position, glm::vec2 size) : Box2D(position, size) {}
    void OnCollisionEnter(entity_id) {
    }
    void OnCollision(entity_id) {
    }
    void OnCollisionLeave(entity_id) {
    }
};
class BallRigidBody : public Box2D {
public:
    BallRigidBody(glm::vec2 position, glm::vec2 size, glm::vec2 & init_ball_velocity) : Box2D(position, size), ball_velocity(init_ball_velocity) {};
    void OnCollisionEnter(entity_id) {
        ball_velocity.x *= -1;
    }
    void OnCollision(entity_id) {
    }
    void OnCollisionLeave(entity_id) {
    }
    glm::vec2 & ball_velocity;

};

void PongScene::ResetRound() {
    PDebug::log("Round finished");
    float deg = rand() % 60;
    deg -= 30;
    PDebug::info(std::format("Ball Initial Degree: {}", deg));
    ballVelocity = glm::vec2(glm::cos(glm::radians(deg)), glm::sin(glm::radians(deg)));
    ballVelocity.x *= -1;
    //ballVelocity = glm::vec2(-0.2f, 0.0f);


    auto& body_interface = ecs.physics_system.GetBodyInterface();

    RigidBody* ball_body = ecs.getRigidBody(ball);
    glm::vec3 pos = glm::vec3(widths / 2.0f, heights / 2.0f, 0.0f);
    ecs.getPosition(ball)->raw_position = pos;
    body_interface.SetPosition(ball_body->jolt_id, JPH::Vec3(pos.x, pos.y, pos.z), JPH::EActivation::Activate);
    #define MAX_ANGLE 0.80f
    
}

void PongScene::OnStart() {

    srand(time(NULL));

    ImGui::LoadIniSettingsFromDisk("space_invaders_imgui.ini");
   
    SetBgColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    float width = sapp_widthf();
    float height = sapp_heightf();


    {

        Camera cam = Camera(width / height);
        cam.configure2D(width, height, ZOOM);
        SetPlayerCamera(cam);
    }

    auto &body_interface = ecs.physics_system.GetBodyInterface();

    { // create player
        player = ecs.createEntity();        

        glm::vec2 pos = glm::vec2(8, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 10.0f));


        PlayerPaddle *paddle_rigid_body = new PlayerPaddle(pos, glm::vec2(2.0f, 10.0f));

        ecs.addPosition(player, quad->position);
        ecs.addMesh(player, quad->mesh);
        ecs.addRigidBody(player, paddle_rigid_body);

    } 

    { // create enemy
        enemy = ecs.createEntity();        

        glm::vec2 pos = glm::vec2(widths - 8, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 10.0f));

        ecs.addPosition(enemy, quad->position);
        ecs.addMesh(enemy, quad->mesh);
        
        PlayerPaddle *paddle_rigid_body = new PlayerPaddle(pos, glm::vec2(2.0f, 10.0f));
        ecs.addRigidBody(enemy, paddle_rigid_body);
       
    } 

    { // create ball
        ball = ecs.createEntity();        

        glm::vec2 pos = glm::vec2(widths / 2.0f, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 2.0f));

        ecs.addPosition(ball, quad->position);
        ecs.addMesh(ball, quad->mesh);
        
        BallRigidBody *paddle_rigid_body = new BallRigidBody(pos, glm::vec2(2.0f, 2.0f), ballVelocity);
        ecs.addRigidBody(ball, paddle_rigid_body);
        ecs.SetMotionType(ball, JPH::EMotionType::Dynamic);
        
    } 


    {
        //ballVelocity = glm::vec2(-0.4f, 0.8f);
        ResetRound();
    }
}

void PongScene::OnEnd() {
    ImGui::SaveIniSettingsToDisk("space_invaders_imgui.ini");
}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    } 
}
    
#define SPEED 0.4f


void PongScene::OnTick(float tick_t) {
    auto& body_interface = ecs.physics_system.GetBodyInterface();
    //auto& collision_group = body_interface.GetCollisionGroup(player_id);

    glm::vec3& bPos = ecs.getPosition(ball)->position;
    glm::vec3& bSize = ecs.getMesh(ball)->scale;

    if (bPos.y + (bSize.y / 2.0f) > heights && ballVelocity.y > 0) {
        ballVelocity.y *= -1;
    }
    if (bPos.y - (bSize.y / 2.0f) < 0.0f && ballVelocity.y < 0) {
        ballVelocity.y *= -1;
    }


    if (bPos.x + (bSize.x / 2.0f) > widths) {
        playerPoints += 1;
        ResetRound();
    }
    if (bPos.x - (bSize.x / 2.0f) < 0.0f) {
        enemyPoints += 1;
        ResetRound();
    }

}

void PongScene::OnUpdate() {
    float direction = 0.0f;
    if (IsKeyPressed(SAPP_KEYCODE_W)) {
        direction += 1.0f;
    }
    if (IsKeyPressed(SAPP_KEYCODE_S)) {
        direction -= 1.0f;
    }


    auto& body_interface = ecs.physics_system.GetBodyInterface();

    #define CALC_SPEED 15.0f * delta_t 

    ecs.setVelocity(player, glm::vec3(0.0f, direction * CALC_SPEED * 1.05f, 0.0f));

    ecs.setVelocity(ball, glm::vec3(ballVelocity.x * CALC_SPEED, ballVelocity.y * CALC_SPEED, 0.0f));


    {
        ImGui::Begin("PongScene", NULL, 0);

        ImGui::Text("Player Points: %d -- Enemy Points: %d", playerPoints, enemyPoints);

        ImGui::Text("Resolution: (%d, %d)", sapp_width(), sapp_height());

        Position* pos = ecs.getPosition(player);
        ImGui::Text("Player: (%.2f, %.2f)", pos->position.x, pos->position.y);
       
        auto vel = body_interface.GetPosition(ball_id);
        ImGui::Text("Ball Position: (%.2f, %.2f, %.2f)", vel.GetX(), vel.GetY(), vel.GetZ());
        vel = body_interface.GetLinearVelocity(ball_id);
        ImGui::Text("Ball Velocity: (%.2f, %.2f, %.2f)", vel.GetX(), vel.GetY(), vel.GetZ());

        ImGui::End();
    }
}
