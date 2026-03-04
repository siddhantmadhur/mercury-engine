#include <Jolt/Jolt.h>
#include <Jolt/Core/Memory.h>

#include "Jolt/Core/Factory.h"
#include "Jolt/Core/JobSystemThreadPool.h"
#include "Jolt/Core/TempAllocator.h"
#include "Jolt/Physics/PhysicsSettings.h"
#include "Jolt/RegisterTypes.h"
#include "ecs/ecs.hh"
#include <gameobjects/camera.hh>
#include <engine/engine.hh>
#include <debugger/debugger.hh>
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
#include <thread>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <iostream>
using namespace std;
using namespace JPH;

void LoadMainMenuBar() {
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
}

PequodEngine::PequodEngine() {
    //this->ecs = ECS();
}


uint64_t PequodEngine::getTicks() {
    return this->ticks;
}

void PequodEngine::startUp() {
    frame_time = 1;
    tick_frame_time = 1;
    delta_t = 0;
    ticks = 0;

    // Create new thread for ticks here
    //ticks_thread = std::thread(run_every_tick, (void*)this);     
}

void PequodEngine::initializeJolt() {
    // Initialize Jolt

    RegisterDefaultAllocator();

    Factory::sInstance = new Factory();

    RegisterTypes();

    TempAllocatorImpl temp_allocator(10 * 1024 * 1024); // 10MB

    JobSystemThreadPool job_system(cMaxPhysicsJobs, cMaxPhysicsBarriers, thread::hardware_concurrency() - 1);
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
    if (show_debug_stats) {
        LoadMainMenuBar();
    }

    delta_t = stm_ms(stm_laptime(&frame_time));
    currentScene->SetDelta(delta_t);
    total_t += delta_t;

    ticks = int(total_t / 50); // 20 

    if (last_tick < ticks) {
        tick_t = stm_ms(stm_laptime(&tick_frame_time));
        currentScene->OnTick(tick_t);
        currentScene->tick = ticks;
        last_tick = ticks;
    }

    if (ticks % 20 == 0) {
        fps = 1000.0 / delta_t;
    }

    if (show_debug_stats) {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus ;
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        if (ImGui::Begin("Status", NULL, flags))
        {
            ImGui::Text("FPS: %d", fps);
            ImGui::SameLine();
            ImGui::Text("Frametime: %.2fms", delta_t);
            ImGui::SameLine();
            ImGui::Text("tps: %.2f", std::ceil(ticks / (total_t / 1000)));
            ImGui::End();
        }
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

bool PequodEngine::isShowDebugStats() {
    return this->show_debug_stats;
}

void PequodEngine::sokol_cleanup() {
    if (currentScene) {
        currentScene->OnEnd();
        currentScene->Deinit();
    }
    simgui_shutdown();
    sg_shutdown();
}

void PequodEngine::sokol_event(const sapp_event *event) {
    simgui_handle_event(event);
    currentScene->handleKeys(event);
    currentScene->OnEvent(event);
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_F1) {
            PDebug::info("toggling debug stats");
            show_debug_stats = !show_debug_stats;
        }
    }
}

