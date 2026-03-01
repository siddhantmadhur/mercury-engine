

#include "assets/images.hh"
#include "gameobjects/camera.hh"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include <sokol/sokol_log.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <shaders/generic_texture.glsl.hh>

#include <engine/scene.hh>

#include <iostream>
using namespace std;

#define INIT_VERTICES 600
#define INIT_INDICES ((INIT_VERTICES)/2)*3
#define COLOR_ONLY 1

#define MAX_VERTICES 5120
#define MAX_INDICES ((MAX_VERTICES)/2)*3

Scene::Scene() : playerCamera(16.0/9.0) {
    vertices = std::vector<vertex_t>();
    indices = std::vector<uint16_t>();
    vertices.reserve(INIT_VERTICES);
    indices.reserve(INIT_INDICES);
}

void Scene::Deinit() {
    for (int i = 0; i < objects.size(); i++) {
        delete objects[i];
    }
    sg_destroy_buffer(bind.vertex_buffers[0]);
    sg_destroy_buffer(bind.index_buffer);
    sg_destroy_view(bind.views[VIEW_tex]);
    sg_destroy_sampler(bind.samplers[SMP_smp]);
}

void Scene::handleKeys(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        keys_pressed[event->key_code] = true;
    } else if (event->type == SAPP_EVENTTYPE_KEY_UP) {
        keys_pressed[event->key_code] = false;
    }
}

void Scene::Init() {
    Image wall_texture = Image("assets/wall.jpg");

 
    bind.views[VIEW_tex] = sg_alloc_view();

    //state.bind.images[IMG_tex] = sg_alloc_image();

    bind.samplers[SMP_smp] = sg_make_sampler((sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .label = "jpg-sampler",
    });

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
    img_desc.data.subimage[0][0] = {
        .ptr = wall_texture.raw_data,
        .size = (size_t) wall_texture.x * wall_texture.y * 4,
    };
    sg_image img = sg_make_image(img_desc);

    sg_init_view(bind.views[VIEW_tex], (sg_view_desc){
        .texture = { .image = img },
        .label = "jpg-texture-view",
    });

}


void Scene::SetDelta(float delta_t) {
    this->delta_t = delta_t;
}


void Scene::Render(float width, float height) {

   
    cam_params_t params;
    params.view = playerCamera.getView();
    params.projection = playerCamera.getProjection();


    size_t vertex_size = vertices.size() * sizeof(vertex_t);
    if (vertices.size() > 0) {
        sg_update_buffer(bind.vertex_buffers[0], (sg_range){
            .ptr = vertices.data(),
            .size = vertex_size,
        });
    }
  

    size_t indices_size = indices.size() * sizeof(uint16_t);
    if (indices.size() > 0) {
        sg_update_buffer(bind.index_buffer, (sg_range){
            .ptr = indices.data(),
            .size = indices_size,
        });
    }


    sg_begin_pass((sg_pass){
        .action = pass_action,
        .swapchain = sglue_swapchain(),
    });

    sg_apply_pipeline(pip);
    
    sg_apply_bindings(bind);

    sg_apply_uniforms(UB_cam_params, SG_RANGE(params));

    //cout << "Rendering " << indices.size() << " indices!" << endl;

    //sg_draw(0, indices.size(), 1);
    // TODO: FOR TOMORROW: MAKE POSITION A BINDABLE VALUE AND SET IT FOR EACH OBJECT BEFORE RENDERING
    // TODO: RIGHT NOW FOR SOME REASON ITS NOT FKING DETERMINISTIC, MOVE DATA FROM VERTEX DATA TO BINDABLE
    for (int i = 0; i < objects.size(); i++) {
        GameObject* obj = objects[i];
        obj->Draw();

    }

}

void Scene::AddObject(GameObject* obj) {
    auto obj_vertex = obj->vertices;
    auto obj_indices = obj->indices;
    
    obj->setId(current_id);
    current_id += obj_indices.size();
    objects.insert(objects.end(), obj);

    int prev_size = vertices.size();

    for (int i = 0; i < obj_indices.size(); i++) {
        obj_indices[i] += prev_size;
    }

    vertices.insert(vertices.end(), obj_vertex.begin(), obj_vertex.end());
    indices.insert(indices.end(), obj_indices.begin(), obj_indices.end());

}

void Scene::SetPlayerCamera(Camera& cam) {
    this->playerCamera = cam;
}

Camera& Scene::GetPlayerCamera() {
    return this->playerCamera;
}

void Scene::SetBgColor(glm::vec4 newColor) {
    bgColor = newColor;
    pass_action = (sg_pass_action){
        .colors = { { .load_action = SG_LOADACTION_CLEAR, .clear_value={bgColor.x, bgColor.y, bgColor.z, bgColor.a}}}
    };
}

bool Scene::IsKeyPressed(sapp_keycode key) {
    if (keys_pressed.contains(key)) {
        return keys_pressed[key];
    } else {
        return false;
    }
}
