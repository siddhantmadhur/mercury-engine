#include <Jolt/Jolt.h>


#include "Jolt/Physics/PhysicsSystem.h"
#include "ecs/ecs.hh"
#include <format>
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
// [CLAUDE] TODO: Duplicate include — glm/glm.hpp already included two lines above
#include <glm/glm.hpp>

#include <shaders/generic_texture.glsl.hh>
#include <stdexcept>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <iostream>
using namespace std;

void LoadMainMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Exit", "Esc")) {
                sapp_quit();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

namespace Pequod {
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


    void PequodEngine::SetScene(WorldScene *scene) {
        currentScene = scene;
    }

    void PequodEngine::sokol_init() {
        currentScene->OnStartInternal();

        currentScene->Initialize();
        // runs only for the initial default scene; every other scene needs to call this themselves

        currentScene->OnStart();
    };


    void PequodEngine::sokol_frame_cb() {
        const int width = sapp_width();
        const int height = sapp_height();

        simgui_new_frame({width, height, sapp_frame_duration(), sapp_dpi_scale()});

        auto pass = sg_pass{ 0 };
        pass.action = currentScene->GetPassAction();
        pass.swapchain = sglue_swapchain();

        sg_begin_pass(&pass);

        {
            currentScene->ComputeTick();
            currentScene->OnFrameUpdate();
            currentScene->SetupRenderState();
            currentScene->RenderObjects();
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
            currentScene->OnDestroy();
            currentScene->Destroy();
        }
        //simgui_shutdown();
        //sg_shutdown();
    }

    void PequodEngine::sokol_event(const sapp_event *event) {
        currentScene->OnEventInternal(event);
    }
}
