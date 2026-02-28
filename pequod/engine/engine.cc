#include <gameobjects/gameobject.hh>
#include <gameobjects/camera.hh>
#include <engine/engine.hh>

#include <assets/images.hh> 

#include <sokol/sokol_gfx.h>
#include <sokol/sokol_app.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/sokol_time.h>

#include <imgui/imgui.h>
#include <sokol/util/sokol_imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/glm.hpp>

#include <shaders/generic_texture.glsl.hh>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <iostream>
using namespace std;

void LoadMainMenuBar() {
#if 1
    if (ImGui::BeginMainMenuBar())
    {
      if (ImGui::BeginMenu("File"))
      {
        if (ImGui::MenuItem("Exit", "Esc"))
        {
          sapp_quit();
        }
        ImGui::EndMenu();
      }
    ImGui::EndMainMenuBar();
    }
#endif
}

PequodEngine::PequodEngine() {
    frame_time = 1;
    delta_t = 0;
}

void PequodEngine::SetScene(Scene* scene) {
    currentScene = scene;
}

void PequodEngine::sokol_init() {
    sg_setup((sg_desc){
        .logger = {
            .func = slog_func,
        },
        .environment = sglue_environment(),
    });

    stm_setup();
    
    simgui_desc_t simgui_desc = { };
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);


    currentScene->Init(); // runs only for the initial default scene; every other scene needs to call this themselves


    currentScene->OnStart();
};



void PequodEngine::sokol_frame_cb() {

    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    //ImGui::ShowDemoWindow();
    LoadMainMenuBar();

    delta_t = stm_ms(stm_laptime(&frame_time));
    currentScene->SetDelta(delta_t);
   
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus ;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    if (ImGui::Begin("Status", nullptr, flags))
    {
        ImGui::Text("Frametime: %.2fms", delta_t);
        ImGui::SameLine();
        ImGui::End();
    }

    currentScene->OnUpdate();

    if (currentScene) {
        currentScene->Render(width, height);
    } else {
        throw std::invalid_argument("current scene not provided");
    }

    simgui_render();
    sg_end_pass();
    sg_commit();
}

void PequodEngine::sokol_cleanup() {
    simgui_shutdown();
    sg_shutdown();
}

void PequodEngine::sokol_event(const sapp_event *event) {
    simgui_handle_event(event);
    currentScene->OnEvent(event);
}

