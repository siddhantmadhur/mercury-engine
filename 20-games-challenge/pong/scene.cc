#include "scene.hh"
#include "Jolt/Math/Quat.h"
#include "Jolt/Math/Real.h"
#include "Jolt/Math/Vec3.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Body/MotionType.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/EActivation.h"
#include "ecs/entity.hh"
#include "engine/scene.hh"
#include "gameobjects/camera.hh"
#include "gameobjects/quad.hh"
#include "glm/fwd.hpp"
#include <format>
#include <iostream>
#include <imgui/imgui.h>
#include <glm/glm.hpp>
#include <debugger/debugger.hh>


#define ZOOM 8.0f
    
#define heights (sapp_heightf() * (1 / ZOOM))
#define widths (sapp_widthf() * (1 / ZOOM))


void PongScene::ResetRound() {
    float deg = rand() % 60;
    deg -= 30;
    PDebug::info(std::format("Ball Initial Degree: {}", deg));
    ballVelocity = glm::vec2(glm::cos(glm::radians(deg)), glm::sin(glm::radians(deg)));
    ballVelocity.x *= -1;

    auto& pos = ecs.getPosition(ball)->raw_position;
    pos = glm::vec3(widths / 2.0f, heights / 2.0f, 0.0f);
    //vector = glm::normalize(vector);
    #define MAX_ANGLE 0.80f
    
}

bool doCollide(glm::vec2 posA, glm::vec2 sizeA, glm::vec2 posB, glm::vec2 sizeB) { // (still object, moving object)
    //glm::vec2 sizeA = A->GetSize();
    //glm::vec2 posA = A->GetPosition();
    posA.x -= sizeA.x / 2.0f;
    posA.y -= sizeA.y / 2.0f;
    bool collisionX = posA.x + sizeA.x >= posB.x && posB.x + sizeB.x >= posA.x;
    bool collisionY = posA.y + sizeA.y >= posB.y && posB.y + sizeB.y >= posA.y;
    return (collisionX && collisionY);
}

void PongScene::OnStart() {
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

        ecs.addPosition(player, quad->position);
        ecs.addMesh(player, quad->mesh);

        glm::vec3 size = glm::vec3(2.0f, 10.0f, 0.0f);

        JPH::BoxShapeSettings player_shape_settings(JPH::Vec3(size.x, size.y, 0.1f));
        player_shape_settings.SetEmbedded();

        JPH::ShapeSettings::ShapeResult player_shape_result = player_shape_settings.Create();
        JPH::ShapeRefC player_shape = player_shape_result.Get();

        JPH::BodyCreationSettings player_settings(player_shape, JPH::RVec3(pos.x, pos.y, 0.0f), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic, Layers::MOVING);

        JPH::Body* player_body = body_interface.CreateBody(player_settings);
        if (player_body == NULL) {
            PDebug::error("Player body is NULL");
        }

        player_id = player_body->GetID();
        body_interface.AddBody(player_id, JPH::EActivation::Activate);
    } 

    { // create enemy
        enemy = ecs.createEntity();        

        glm::vec2 pos = glm::vec2(widths - 8, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 10.0f));

        ecs.addPosition(enemy, quad->position);
        ecs.addMesh(enemy, quad->mesh);
    } 

    { // create ball
        ball = ecs.createEntity();        

        glm::vec2 pos = glm::vec2(widths / 2.0f, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 2.0f));

        ecs.addPosition(ball, quad->position);
        ecs.addMesh(ball, quad->mesh);
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

    if (tick % 8 == 0) {
        return;
    }

    auto& bPos = ecs.getPosition(ball)->raw_position;
    auto &ePos = ecs.getPosition(enemy)->raw_position;

        float diff = bPos.y - ePos.y;

    if (ePos.x - bPos.x < 32) {
        #define MAX_STEP 3.0f
        if (glm::abs(diff) > 5.0) {
            ePos.y += (diff / glm::abs(diff))*2.0f;
        } else {
            ePos.y += diff ;
        }
    } else {
        ePos.y += (diff / glm::abs(diff))*0.05f;
    }
    
    float direction = 0.0f;
    if (IsKeyPressed(SAPP_KEYCODE_W)) {
        direction += 1.0f;
    }
    if (IsKeyPressed(SAPP_KEYCODE_S)) {
        direction -= 1.0f;
    }

    //Position* pos = ecs.getPosition(player);
    //pos->raw_position.y += direction * SPEED * 4.0f;

    auto& body_interface = ecs.physics_system.GetBodyInterface();

    body_interface.SetLinearVelocity(player_id, JPH::Vec3(0.0f, direction * 0.8, 0.0f));

    //auto &rMesh = ecs.getMesh(player)->scale;

    //rPos.y = glm::max(rPos.y, rMesh.y / 2.0f); 
    //rPos.y = glm::min(rPos.y, heights - (rMesh.y / 2.0f)); 

    //if (doCollide(pos->raw_position, ))

    auto& ballPos = ecs.getPosition(ball)->raw_position;
    auto& bMesh = ecs.getMesh(ball)->scale;
    ballPos += glm::vec3(ballVelocity.x, ballVelocity.y, 0.0f) * 0.05f;

    
    if (ecs.doesCollide(enemy, ball) && ballVelocity.x > 0) {
        ballVelocity.x *= -1;
        ballPos.x = ePos.x - (ecs.getMesh(ball)->scale.x / 2.0f); 
    }

    if (ballPos.y + (bMesh.y/2.0f) > heights ) {
        ballPos.y = (heights) - (ecs.getMesh(ball)->scale.y / 2.0f);
        ballVelocity.y *= -1; 
    }
    if (ballPos.y - (bMesh.y/2.0f) < 0 ) {
        ballPos.y = (ecs.getMesh(ball)->scale.y / 2.0f);
        ballVelocity.y *= -1; 
    }


    if (ballPos.x < 0 || ballPos.x > widths) {
        ResetRound();
    }
    
    ecs.physics_system.Update(delta_t, 1, ecs.temp_allocator, ecs.job_system);

}

void PongScene::OnUpdate() {
 
    

    auto& body_interface = ecs.physics_system.GetBodyInterface();
    auto pos = body_interface.GetPosition(player_id);

    auto* vPos = ecs.getPosition(player);
    vPos->raw_position = glm::vec3(pos.GetX(), pos.GetY(), 0.0f);


    {
        ImGui::Begin("PongScene", NULL, 0);
        
        ImGui::Text("Resolution: (%d, %d)", sapp_width(), sapp_height());

        //Position* pos = ecs.getPosition(player);
        ImGui::Text("Player: (%.2f, %.2f)", pos.GetX(), pos.GetY());

        ImGui::End();
    }
}
