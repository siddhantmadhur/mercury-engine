#include "editor_main.h"
#include "imgui/imgui.h"
#include "panel/game_preview.hh"

#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/sokol_time.h>
#include <sokol/util/sokol_imgui.h>

#include "tinyxml2.h"


namespace Pequod {

Editor::Editor(EditorScene& defaultScene) : currentScene(defaultScene) 
{
    const char *home_dir = getenv("HOME");
    editor_cwd = fs::path(home_dir) / "pequod";
}

void Editor::InitializeEditor() {
    fs::path config_file = editor_cwd / "editor_config.xml";
}

void Editor::SetScene(EditorScene& scene) {
    currentScene = scene;
}

void Editor::sokol_init() {

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


    float gray = 73.0f / 255.0f;
    pass_action.colors[0] = {
        .load_action = SG_LOADACTION_CLEAR,
        .clear_value = { gray, gray, gray, 1.0f },
    };

    game_scene = std::make_unique<GamePreviewScene>();
    //game_scene->OnStartInternal();
    game_scene->Initialize();
    game_scene->OnStart();

    currentScene.InitializeScene();

}

void Editor::sokol_frame_cb() {
    const int width = sapp_width();
    const int height = sapp_height();



    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    {
        currentScene.OnFrameInternal();
        currentScene.RenderScenePreview((WorldScene**) &game_scene);
    }
    
    sg_begin_pass((sg_pass){
        .action = game_scene->GetPassAction(),
        .swapchain = sglue_swapchain(),
    });
    simgui_render();

    sg_end_pass();

    sg_commit();
}

void Editor::sokol_cleanup() {
    currentScene.OnDestroyInternal();

    simgui_shutdown();
    sg_shutdown();
}

void Editor::sokol_event(const sapp_event* event) {
    simgui_handle_event(event);
    currentScene.OnEventInternal(event);
}

}
