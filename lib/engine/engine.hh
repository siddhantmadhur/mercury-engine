#ifndef MERCURY_ENGINE_IMPL_HH
#define MERCURY_ENGINE_IMPL_HH


#include <cstdint>
#include <engine/scene.hh>

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>


//#include <engine/engine.cc>



typedef struct vertext_t {
    float x, y, z;
    int16_t u, v;
} vertex_t;
    
class Mercury
{
public:
    Mercury();
    void sokol_init();
    void sokol_frame_cb();
    void sokol_cleanup();
    void sokol_event(const sapp_event* event);

private:
    uint64_t frame_time;
    double delta_t;
    sg_pipeline pip;
    sg_pass_action pass_action;
    sg_bindings bind;
};

#endif
