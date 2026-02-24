#include <engine/engine.hh>

#include <assets/images.hh> 
#include <gameobjects/camera.hh>
#include <gameobjects/cube.hh>

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

#include <generic_texture.glsl.hh>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>


#include <iostream>
using namespace std;

void LoadMainMenuBar() {
#if 1
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

Mercury::Mercury() {
    frame_time = 1;
    delta_t = 0;
}

void Mercury::sokol_init() {

    Image wall_texture = Image("wall.jpg");

    auto cam = GameObject::Camera();

    sg_setup((sg_desc){
        .logger = {
            .func = slog_func,
        },
        .environment = sglue_environment(),
    });

    stm_setup();
  
    bind.views[VIEW_tex] = sg_alloc_view();
    //state.bind.images[IMG_tex] = sg_alloc_image();

    bind.samplers[SMP_smp] = sg_make_sampler((sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .label = "jpg-sampler",
    });
    sg_shader shd = sg_make_shader(simple_shader_desc(sg_query_backend()));

    #define SHORT_MAX 32767
    const vertex_t vertices[] = {
    { -1.0f, -1.0f, -1.0f,      0,     0 },
        {  1.0f, -1.0f, -1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f, -1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f,  1.0f, -1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f,  1.0f,      0,     0 },
        {  1.0f, -1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f,  1.0f,  1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f, -1.0f,      0,     0 },
        { -1.0f,  1.0f, -1.0f,  SHORT_MAX,     0 },
        { -1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        { -1.0f, -1.0f,  1.0f,      0, SHORT_MAX },

        {  1.0f, -1.0f, -1.0f,      0,     0 },
        {  1.0f,  1.0f, -1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f, -1.0f,  1.0f,      0, SHORT_MAX },

        { -1.0f, -1.0f, -1.0f,      0,     0 },
        { -1.0f, -1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f, -1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f, -1.0f, -1.0f,      0, SHORT_MAX },

        { -1.0f,  1.0f, -1.0f,      0,     0 },
        { -1.0f,  1.0f,  1.0f,  SHORT_MAX,     0 },
        {  1.0f,  1.0f,  1.0f,  SHORT_MAX, SHORT_MAX },
        {  1.0f,  1.0f, -1.0f,      0, SHORT_MAX },

    };

    bind.vertex_buffers[0] = sg_make_buffer((sg_buffer_desc){
        .size = sizeof(vertices),
        .data = SG_RANGE(vertices),
        .label = "quad-vertices"
    });

    uint16_t indices[] = {
    0, 1, 2,  0, 2, 3,
        6, 5, 4,  7, 6, 4,
        8, 9, 10,  8, 10, 11,
        14, 13, 12,  15, 14, 12,
        16, 17, 18,  16, 18, 19,
        22, 21, 20,  23, 22, 20};

    auto index_buffer_bind = (sg_buffer_desc){
        .size = sizeof(indices),
        .data = SG_RANGE(indices),
        .label = "quad-indices",
    };

    index_buffer_bind.usage.index_buffer = true;
    bind.index_buffer = sg_make_buffer(index_buffer_bind);

    

    sg_pipeline_desc pip_desc = (sg_pipeline_desc){
        .shader = shd,
        .depth = {
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


    simgui_desc_t simgui_desc = { };
    simgui_desc.logger.func = slog_func;
    simgui_setup(&simgui_desc);

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

    sg_init_view(bind.views[VIEW_tex], (sg_view_desc){
        .texture = { .image = img },
        .label = "jpg-texture-view",
    });
};



void Mercury::sokol_frame_cb() {

    const int width = sapp_width();
    const int height = sapp_height();
    simgui_new_frame({ width, height, sapp_frame_duration(), sapp_dpi_scale() });

    //ImGui::ShowDemoWindow();
    //LoadMainMenuBar();

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


    glm::vec3 position = glm::vec3(0, 0, -5);
    glm::vec3 front = glm::vec3(0, 0, -1);
    glm::vec3 up = glm::vec3(0, 1, 0);

    int pitch = 0;
    int yaw = 90;

    glm::mat4 Proj = glm::perspective<float>(glm::radians(60.0f), (float) ((float)width / height), 0.1f, 10.0f);
   // glm::mat4 View = glm::lookAt(position, position + front, up);
    glm::mat4 View = glm::lookAt(glm::vec3(0.0f, 1.5f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
   
    vs_params_t params;
    params.mvp = Proj * View;




    sg_begin_pass((sg_pass){
        .action = pass_action,
        .swapchain = sglue_swapchain(),
    });

    sg_apply_pipeline(pip);
    
    sg_apply_uniforms(UB_vs_params, SG_RANGE(params));
    
    sg_apply_bindings(bind);
    sg_draw(0, 36, 1);

    simgui_render();
    sg_end_pass();
    sg_commit();
}

void Mercury::sokol_cleanup() {
    simgui_shutdown();
    sg_shutdown();
}

void Mercury::sokol_event(const sapp_event *event) {
    simgui_handle_event(event);
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}

