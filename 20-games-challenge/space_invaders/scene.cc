#include "scene.hh"
#include "ecs/entity.hh"
#include "engine/scene.hh"
#include "gameobjects/camera.hh"
#include "gameobjects/quad.hh"
#include <iostream>
#include <imgui/imgui.h>

#define ZOOM 8.0f

void SpaceInvaders::OnStart() {
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

    
    {
        entity_id player = ecs.createEntity();        
        //ecs.addMesh(player, )
        Quad quad = Quad();
        ecs.addMesh(player, quad.mesh);

        quad.position->raw_position += glm::vec3(widths / 2.0f, heights / 2.0f, 0.0f);
        ecs.addPosition(player, quad.position);
    } 
}

void SpaceInvaders::OnEnd() {
    ImGui::SaveIniSettingsToDisk("space_invaders_imgui.ini");
}

void SpaceInvaders::OnEvent(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    } else if (event->type == SAPP_EVENTTYPE_RESIZED) {

    }
}

void SpaceInvaders::OnTick(float tick_t) {}

void SpaceInvaders::OnUpdate() {
    ImGui::Begin("SpaceInvaders", NULL, 0);
    
    ImGui::Text("Resolution: (%d, %d)", sapp_width(), sapp_height());

    ImGui::End();
}
