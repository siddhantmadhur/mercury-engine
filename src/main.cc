#include <iostream>

#include "variables.hh"

#define SOKOL_IMPL
#define SOKOL_GLCORE
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_app.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_log.h>
#include <sokol/sokol_time.h>

#include <imgui/imgui.h>
#include <sokol/util/sokol_imgui.h>

#include "1-triangle.shader.hh"

//#include <engine/engine.cc>


static struct {
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
} state;

void engine_init() {
    sg_setup((sg_desc){
        .logger = {
            .func = slog_func,
        },
        .environment = sglue_environment(),
    });

    stm_setup();

    sg_shader shd = sg_make_shader(simple_shader_desc(sg_query_backend()));

    float vertices[] = {
         0.5f,  0.5f, 0.0f, // top right
         0.5f, -0.5f, 0.0f, // bottom right
        -0.5f, -0.5f, 0.0f, // bottom left
        -0.5f,  0.5f, 0.0f, // top left
    };

    state.bind.vertex_buffers[0] = sg_make_buffer((sg_buffer_desc){
        .size = sizeof(vertices),
        .data = SG_RANGE(vertices),
        .label = "quad-vertices"
    });

    uint16_t indices[] = {
        0, 1, 3,
        1, 2, 3
    };

    auto index_buffer_bind = (sg_buffer_desc){
        .size = sizeof(indices),
        .data = SG_RANGE(indices),
        .label = "quad-indices",
    };

    index_buffer_bind.usage.index_buffer = true;
    state.bind.index_buffer = sg_make_buffer(index_buffer_bind);

    

    sg_pipeline_desc pip_desc = (sg_pipeline_desc){
        .shader = shd,
        .label = "triangle-pipeline",
    };

    pip_desc.index_type = SG_INDEXTYPE_UINT16;

    pip_desc.layout.attrs[ATTR_simple_position].format = SG_VERTEXFORMAT_FLOAT3;
    state.pip = sg_make_pipeline(pip_desc);


    simgui_desc_t simgui_desc = { };
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);

    state.pass_action = (sg_pass_action){
        .colors = { { .load_action = SG_LOADACTION_CLEAR, .clear_value={0.2f, 0.3f, 0.3f, 1.0f}}}
    };
}

void LoadMainMenuBar() {
#if DEBUG
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

uint64_t frame_time = 0;
double delta_t = 1;
void engine_frame() {

    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    //ImGui::ShowDemoWindow();
    LoadMainMenuBar();


    ;
    delta_t = stm_ms(stm_laptime(&frame_time));
   
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus ;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    if (ImGui::Begin("Status", nullptr, flags))
    {
        ImGui::Text("Frametime: %.2fms", delta_t);
        ImGui::SameLine();
        ImGui::End();
    }

    sg_begin_pass((sg_pass){
        .action = state.pass_action,
        .swapchain = sglue_swapchain(),
    });

    sg_apply_pipeline(state.pip);
    sg_apply_bindings(state.bind);
    sg_draw(0, 6, 1);

    simgui_render();
    sg_end_pass();
    sg_commit();
}

void engine_event_cb(const sapp_event* event) {
    simgui_handle_event(event);
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}
void engine_cleanup() {
    simgui_shutdown();
    sg_shutdown();
}

sapp_desc sokol_main(int argc, char *argv[]) {
    return (sapp_desc){
        .init_cb = engine_init,
        .frame_cb = engine_frame,
        .cleanup_cb = engine_cleanup,
        .event_cb = engine_event_cb,
        .width = 1280,
        .height = 720,
        .window_title = "Mercury Engine",
        .logger = {
            .func = slog_func,
        },
    };
}
