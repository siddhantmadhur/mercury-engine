#include "assets/images.hh"
#include "gameobjects/camera.hh"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include <sokol/sokol_log.h>
#include <sokol/sokol_gfx.h>
#include <sokol/sokol_glue.h>
#include <sokol/sokol_time.h>
#include <sokol/sokol_time.h>
#include <generic_texture.glsl.hh>

#include <engine/scene.hh>

#include <iostream>
using namespace std;

#define INIT_VERTEX_SIZE 100

#define MAX_VERTICES 5120
#define MAX_INDICES ((MAX_VERTICES)/2)*3

Scene::Scene() {
    vertices.reserve(MAX_VERTICES);

    /**
    raw_indices = new uint16_t[MAX_INDICES];
    for (int i = 0; i < MAX_INDICES; i++) {
        raw_indices[i + 0] = ((i / 3) * 2) + 0 ;
        raw_indices[i + 1] = ((i / 3) * 2) + 1 ;
        raw_indices[i + 2] = ((i / 3) * 2) + 2 ;
        i += 3;
    }

    **/

}

Scene::~Scene() {
    //delete raw_indices;
}

#define COLOR_ONLY 1

void Scene::Init() { // run after sokol_init
    Image wall_texture = Image("wall.jpg");

    auto cam = Camera();
 
    #if COLOR_ONLY
    bind.views[VIEW_tex] = sg_alloc_view();

    //state.bind.images[IMG_tex] = sg_alloc_image();

    bind.samplers[SMP_smp] = sg_make_sampler((sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .label = "jpg-sampler",
    });

#endif
    sg_shader shd = sg_make_shader(simple_shader_desc(sg_query_backend()));

// create obj here somewhere

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
        /**
        .data = sg_range {
            .ptr = raw_indices,
            .size = sizeof(uint16_t) * MAX_INDICES,
        },
        **/
        .label = "quad-indices",
    });

    //bind.index_buffer = sg_alloc_buffer();
    //bind.vertex_buffers[0] = sg_alloc_buffer();
    
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

    pip = sg_make_pipeline(pip_desc);


    pass_action = (sg_pass_action){
        .colors = { { .load_action = SG_LOADACTION_CLEAR, .clear_value={0.2f, 0.3f, 0.3f, 1.0f}}}
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

    #if COLOR_ONLY
    sg_init_view(bind.views[VIEW_tex], (sg_view_desc){
        .texture = { .image = img },
        .label = "jpg-texture-view",
    });
#endif

}





void Scene::Render(float width, float height) {
    glm::vec3 position = glm::vec3(0, 0, -5);
    glm::vec3 front = glm::vec3(0, 0, -1);
    glm::vec3 up = glm::vec3(0, 1, 0);

    int pitch = 0;
    int yaw = 90;

    glm::mat4 Proj = glm::perspective<float>(glm::radians(60.0f), (float) ((float)width / height), 0.1f, 10.0f);
   // glm::mat4 View = glm::lookAt(position, position + front, up);
    glm::mat4 View = glm::lookAt(glm::vec3(0.0f, 3.5f, 7.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
   
    vs_params_t params;
    params.mvp = Proj * View;



    size_t vertex_size = vertices.size() * sizeof(vertex_t);
    sg_update_buffer(bind.vertex_buffers[0], (sg_range){
        .ptr = vertices.data(),
        .size = vertex_size,
    });
   
    size_t indices_size = indices.size() * sizeof(uint16_t);
    sg_update_buffer(bind.index_buffer, (sg_range){
        .ptr = indices.data(),
        .size = indices_size,
    });


    sg_begin_pass((sg_pass){
        .action = pass_action,
        .swapchain = sglue_swapchain(),
    });

    sg_apply_pipeline(pip);
    
    sg_apply_uniforms(UB_vs_params, SG_RANGE(params));
    
    sg_apply_bindings(bind);

    //cout << "Rendering " << indices.size() << " indices!" << endl;

    sg_draw(0, indices.size(), 1);
}

void Scene::AddObject(GameObject& obj) {
    auto obj_vertex = obj.vertices;
    auto obj_indices = obj.indices;



    int prev_size = vertices.size();
    for (int i = 0; i < obj_indices.size(); i++) {
        
        obj_indices[i] += prev_size;
    }
    indices.insert(indices.end(), obj_indices.begin(), obj_indices.end());

    vertices.insert(vertices.end(), obj_vertex.begin(), obj_vertex.end());


    cout << "Current size " << indices.size() << endl;

    //index_buffer_bind.usage.dynamic_update = true;
    ///sg_init_buffer(bind.index_buffer, index_buffer_bind);


// todo for myself tomorrow: move all the init code from engine class to scene class 

    
    std::cout << "Created New Object!" << std::endl;
    obj.setId(current_id);
    current_id += 1;
}
