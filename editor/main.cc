#include <editor_windows/editor_main.hh>

#include <iostream>

#include <ecs/ecs.hh>

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_app.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/sokol_time.h>

#include <imgui/imgui.h>
#include <sokol/util/sokol_imgui.h>

#include <editor_windows/editor_project.hh>

static Pequod::ProjectSelectionScene selection_scene;
static Pequod::Editor pequod_editor_main(selection_scene);

void sokol_init() {
    pequod_editor_main.sokol_init();
};
void sokol_frame_cb() {
    pequod_editor_main.sokol_frame_cb();
};
void sokol_cleanup() {
    PDebug::info("pequod editor: closing");
    pequod_editor_main.sokol_cleanup();
};
void sokol_event(const sapp_event* event) {
    pequod_editor_main.sokol_event(event);
};

sapp_desc sokol_main(int argc, char *argv[]) {

    PDebug::info("pequod editor: starting");

    selection_scene.InitializeScene();
    pequod_editor_main.InitializeEditor();

    return (sapp_desc){
        .init_cb = sokol_init,
        .frame_cb = sokol_frame_cb,
        .cleanup_cb = sokol_cleanup,
        .event_cb = sokol_event,
        .width = 1280,
        .height = 720,
        .window_title = "Pequod Editor [v0.0.1]",
        .logger = {
            .func = slog_func,
        },
    };;
}

