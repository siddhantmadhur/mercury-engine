#include "scene.hh"
#include "engine/scene.hh"
#include "glm/common.hpp"
#include "glm/trigonometric.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gameobjects/gameobject.hh>
#include <gameobjects/quad.hh>
#include <imgui/imgui.h>
#include <iostream>

#define ZOOM 2.0f
#define SCALE (1.0f/ZOOM) 
#define width 1280 * SCALE
#define height 720 * SCALE

#define width_s width * SCALE
#define height_s height * SCALE

void BreakoutScene::OnStart() {

    ImGui::LoadIniSettingsFromDisk("breakout_scene.ini");

    SetBgColor(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    { // CAMERA

        // TODO: add createOrtho and createPerspective in the engine (maybe to scene?) itself to reduce this code as well


        Camera playerCam = Camera(16.0/9.0);
        glm::vec3 pos = glm::vec3(0.0f, 0.0f, 0.0f);
        playerCam.setPosition(pos);
        
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(-pos.x, pos.y, 0.0f));
        float zoom = ZOOM;
        view = glm::scale(view, glm::vec3(zoom, zoom, 1.0f));


        glm::mat4 proj = glm::ortho<float>(0, width, 0, height, -1.0f, 1.0f);

        playerCam.setProj(proj);
        playerCam.setView(view);

        SetPlayerCamera(playerCam);

    }


    { // PLAYER
        this->player = new Shapes::Quad(glm::vec2(12.0f, 2.0f), glm::vec2(width_s, 16), glm::vec4(1.0f));
        AddObject(*player);
    }


    { // BRICKS 
        float padding_around = 8.0f;
        float total_width = width_s + (-padding_around * 2)  - (10);
        glm::vec2 brick_size = glm::vec2(width_s / 20, 2.0f);
        glm::vec2 offset = glm::vec2(padding_around + (brick_size.x), height_s - brick_size.y - 8);

        for (int j = 0; j < 8; j++) {
            offset.x = padding_around + (brick_size.x);
            for (int i = 0; i < 9; i++) {
                Shapes::Quad* brick = new Shapes::Quad(brick_size, offset, glm::vec4(1.0 / i, 1.0 / j, 0.8f, 1.0f));
                offset.x += brick_size.x * 2;
                offset.x += 2;
                AddObject(*brick);
                bricks.push_back(brick);
            }
            offset.y -= brick_size.y * 2;
            offset.y -= 2;
        }
    }


}


#define playerPos player->GetPosition()
#define playerSize player->GetSize()

void BreakoutScene::OnUpdate() {
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

    #define PLAYER_SPEED 0.48f

    if (player) {
        player->Move(glm::vec3(direction * delta_t * PLAYER_SPEED, 0.0f, 0.0f));
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


    ImGui::Begin("pos", NULL, 0);
    
    ImGui::Text("player: (%.2f, %.2f)", playerPos.x, width);

    ImGui::End();

}
void BreakoutScene::OnEnd() {
    ImGui::SaveIniSettingsToDisk("breakout_scene.ini");
}
void BreakoutScene::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}
