#include "editor_main.hh"
#include "imgui/imgui.h"

#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/util/sokol_imgui.h>


namespace Pequod {

Editor::Editor(EditorScene& defaultScene) : currentScene(defaultScene) 
{
    
}

void Editor::InitializeEditor() {
    
}

void Editor::SetScene(EditorScene& scene) {
    currentScene = scene;
}

void Editor::sokol_init() {
    sg_desc desc = (sg_desc) {
        .logger = {
            .func = slog_func,
        },
        .environment = sglue_environment(),
    };

    float gray = 73.0f / 255.0f;
    pass_action.colors[0] = {
        .load_action = SG_LOADACTION_CLEAR,
        .clear_value = { gray, gray, gray, 1.0f },
    };

    sg_setup(&desc);

    simgui_desc_t simgui_desc = {
        .logger = {
            .func = slog_func,
        }
    };

    simgui_setup(&simgui_desc);

}

void Editor::sokol_frame_cb() {
    const int width = sapp_width();
    const int height = sapp_height();


    sg_begin_pass((sg_pass){
        .action = pass_action,
        .swapchain = sglue_swapchain(),
    });

    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    { 
        currentScene.OnFrameInternal();
    }
    
    simgui_render();

    sg_end_pass();

    sg_commit();
}

void Editor::sokol_cleanup() {
    simgui_shutdown();
    sg_shutdown();
}

void Editor::sokol_event(const sapp_event* event) {
    simgui_handle_event(event);
    currentScene.OnEventInternal(event);
}

}
