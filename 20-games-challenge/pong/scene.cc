#include "scene.hh"
#include "ecs/entity.hh"
#include "engine/scene.hh"
#include "gameobjects/camera.hh"
#include "gameobjects/quad.hh"
#include <iostream>
#include <imgui/imgui.h>
#include <glm/glm.hpp>

#define ZOOM 8.0f

void PongScene::OnStart() {
    ImGui::LoadIniSettingsFromDisk("space_invaders_imgui.ini");
   
    SetBgColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    float width = sapp_widthf();
    float height = sapp_heightf();
    float widths = width * (1 / ZOOM);
    float heights = height * (1 / ZOOM);

    {

        Camera cam = Camera(width / height);
        cam.configure2D(width, height, ZOOM);
        SetPlayerCamera(cam);
    }

    { // create player
        player = ecs.createEntity();        
        glm::vec2 pos = glm::vec2(8, heights / 2.0f);
        Quad *quad = new Quad(pos, glm::vec2(2.0f, 10.0f));
        ecs.addPosition(player, quad->position);
        ecs.addMesh(player, quad->mesh);
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

}

void PongScene::OnEnd() {
    ImGui::SaveIniSettingsToDisk("space_invaders_imgui.ini");
}

void PongScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    } else if (event->type == SAPP_EVENTTYPE_RESIZED) {

    }
}

void PongScene::OnTick() {}

void PongScene::OnUpdate() {
    ImGui::Begin("PongScene", NULL, 0);
    
    ImGui::Text("Resolution: (%d, %d)", sapp_width(), sapp_height());

    auto pos = ecs.getPosition(player);
    if (pos) {
        ImGui::Text("Player: (%.2f, %.2f)", pos->raw_position.x, pos->raw_position.y);
    }

    ImGui::End();
}
