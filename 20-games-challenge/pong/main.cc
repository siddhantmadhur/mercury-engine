#include <iostream>

#include "scene.hh"
#include <engine/engine.hh>

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_app.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/sokol_time.h>

#include <imgui/imgui.h>
#include <sokol/util/sokol_imgui.h>


PequodEngine* engine = NULL;

void sokol_init() {
    engine->sokol_init();
};
void sokol_frame_cb() {
    engine->sokol_frame_cb();
};
void sokol_cleanup() {
    engine->sokol_cleanup();
};
void sokol_event(const sapp_event* event) {
    engine->sokol_event(event);
};

sapp_desc sokol_main(int argc, char *argv[]) {


    engine = new PequodEngine();    


    //Scene *demoScene = new Scene();

    PongScene* demoScene = new PongScene();

    engine->SetScene(demoScene);

    return (sapp_desc){
        .init_cb = sokol_init,
        .frame_cb = sokol_frame_cb,
        .cleanup_cb = sokol_cleanup,
        .event_cb = sokol_event,
        .width = 1280,
        .height = 720,
        .window_title = "Pong Demo - Pequod",
        .logger = {
            .func = slog_func,
        },
    };;
}

