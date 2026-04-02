#define target_tps  60

#include "assets/images.hh"
#include "ecs/ecs.hh"
#include "ecs/entity.hh"
#include "gameobjects/camera.hh"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/ext/vector_float4.hpp"
#include <sokol/sokol_log.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <shaders/generic_texture.glsl.hh>

#include <engine/world_scene.h>

#include <iostream>

#include "sokol/util/sokol_imgui.h"
using namespace std;

#define INIT_VERTICES 600
#define INIT_INDICES (((INIT_VERTICES)/2)*3)
#define COLOR_ONLY 1

#define MAX_VERTICES 5120
#define MAX_INDICES (((MAX_VERTICES)/2)*3)

namespace Pequod {

WorldScene::WorldScene() : playerCamera(1.0f) {
    this->delta_t = 0.0f;
    this->current_tick = 0;
    this->tick_time = 0;
    this->frame_time = 0;
    this->ecs = std::make_shared<ECS>();
    this->physics_engine = std::make_shared<PhysicsEngine>(this->ecs);
    this->object_manager = std::make_shared<PObjectManager>(this->ecs);
}

void WorldScene::ComputePhysics(int steps) {
    //ecs->simulatePhysics(steps);
    physics_engine->Compute(steps);
}

void WorldScene::OnFrameInternal() {
    // TODO: Add an internal width and height that is updated on event
    //  updates and does not require a call to sokol
    const int width = sapp_width();
    const int height = sapp_height();

    ComputeTick();

    BeginRenderPass(width, height);
    OnFrameUpdate();
    CompleteRender();
}

void WorldScene::ComputeTick() {
    delta_t = stm_ms(stm_laptime(&frame_time));
    elapsed_t += delta_t;


    current_tick = int(elapsed_t / (1000.0/target_tps));

    bool run_tick = false;


    if (last_processed_tick < current_tick) {
        tick_t = stm_ms(stm_laptime(&tick_time));

        OnTickUpdate(tick_t);

        last_processed_tick = current_tick;

        run_tick = true;
    }


    if (run_tick) {
        if (tick_t > 0 && delta_t > 0) {
            ComputePhysics(std::round(tick_t / delta_t));
        }
    }

}

void WorldScene::Destroy() {
    /**
    for (int i = 0; i < objects.size(); i++) {
        delete objects[i];
    }
        **/
    sg_destroy_buffer(bind.vertex_buffers[0]);
    sg_destroy_buffer(bind.index_buffer);
    //sg_destroy_view(bind.views[VIEW_tex]);
    //sg_destroy_sampler(bind.samplers[SMP_smp]);
}

void WorldScene::handleKeys(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        keys_pressed[event->key_code] = true;
    } else if (event->type == SAPP_EVENTTYPE_KEY_UP) {
        keys_pressed[event->key_code] = false;
    }
}

void WorldScene::OnStartInternal() {
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
}

void WorldScene::OnEventInternal(const sapp_event *event) {
    simgui_handle_event(event);
    handleKeys(event);
    OnEvent(event);
}

WorldScene::~WorldScene() {
}

void WorldScene::Initialize() {


    Image wall_texture = Image("assets/wall.jpg");


    //bind.views[VIEW_tex] = sg_alloc_view();

    //bind.images[IMG_tex] = sg_alloc_image();

    /**
    bind.samplers[SMP_smp] = sg_make_sampler((sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .label = "jpg-sampler",
    });
    **/

    sg_shader shd = sg_make_shader(simple_shader_desc(sg_query_backend()));

    size_t vertex_size = MAX_VERTICES * sizeof(vertex_t);
    bind.vertex_buffers[0] = sg_make_buffer((sg_buffer_desc){
        .size = vertex_size,
        .usage = {.dynamic_update = true},
        .label = "quad-vertices",
    });

    size_t indices_size = MAX_INDICES * sizeof(uint16_t);
    bind.index_buffer = sg_make_buffer((sg_buffer_desc){
        .size = indices_size,
        .usage = {
            .index_buffer = true,
            .dynamic_update = true
        },
        .label = "quad-indices",
    });

    sg_pipeline_desc pip_desc = (sg_pipeline_desc){
        .shader = shd,
        .depth = {
            .pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
            .write_enabled = true,
        },
        .cull_mode = SG_CULLMODE_BACK,
        .label = "triangle-pipeline",
    };


    pip_desc.index_type = SG_INDEXTYPE_UINT16;

    pip_desc.layout.attrs[ATTR_simple_position].format = SG_VERTEXFORMAT_FLOAT3;
    pip_desc.layout.attrs[ATTR_simple_texcoord0].format = SG_VERTEXFORMAT_SHORT2N;
    pip_desc.layout.attrs[ATTR_simple_color0].format = SG_VERTEXFORMAT_FLOAT4;

    pip = sg_make_pipeline(pip_desc);


    pass_action = (sg_pass_action){
        .colors = { { .load_action = SG_LOADACTION_CLEAR, .clear_value={bgColor.x, bgColor.y, bgColor.z, bgColor.a}}}
    };


    auto img_desc = (sg_image_desc) {
        .width = wall_texture.x,
        .height = wall_texture.y,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .label = "jpg-image"
    };
    img_desc.data.mip_levels[0] = {
        .ptr = wall_texture.raw_data,
        .size = (size_t) wall_texture.x * wall_texture.y * 4,
    };

    /**
    sg_image img = sg_make_image(img_desc);

    sg_init_view(bind.views[VIEW_tex], (sg_view_desc){
        .texture = { .image = img },
        .label = "jpg-texture-view",
    });
    **/

}

void WorldScene::SetupRenderState() {
    cam_params_t params;
    params.view = playerCamera.getView();
    params.projection = playerCamera.getProjection();
    sg_apply_pipeline(pip);
    sg_apply_uniforms(UB_cam_params, SG_RANGE(params));
}

void WorldScene::BeginRenderPass(float width, float height) {
    sg_begin_pass((sg_pass){
        .action = pass_action,
        .swapchain = sglue_swapchain(),
    });

    simgui_new_frame((simgui_frame_desc_t){
        .width = sapp_width(),
        .height = sapp_height(),
        .delta_time = delta_t
    });

    SetupRenderState();
}

void WorldScene::RenderObjects() {
    ecs->setupRender(bind);
    sg_apply_bindings(bind);
    ecs->render(playerCamera, delta_t);
}

void WorldScene::CompleteRender() {


    //cout << "Rendering " << indices.size() << " indices!" << endl;


    simgui_render();
    sg_end_pass();
    sg_commit();
}

void WorldScene::SetPlayerCamera(Camera& cam) {
    this->playerCamera = cam;
}

Camera& WorldScene::GetPlayerCamera() {
    return this->playerCamera;
}

void WorldScene::SetBgColor(glm::vec4 newColor) {
    bgColor = newColor;
    pass_action = (sg_pass_action){
        .colors = { { .load_action = SG_LOADACTION_CLEAR, .clear_value={bgColor.x, bgColor.y, bgColor.z, bgColor.a}}}
    };
}

bool WorldScene::IsKeyPressed(sapp_keycode key) {
    if (keys_pressed.contains(key)) {
        return keys_pressed[key];
    } else {
        return false;
    }
}

sg_pass_action WorldScene::GetPassAction() {
    return this->pass_action;
}
}