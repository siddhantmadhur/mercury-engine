#ifndef MERCURY_ENGINE_IMPL_HH
#define MERCURY_ENGINE_IMPL_HH


#include <cstdint>
#include <engine/scene.hh>

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>


//#include <engine/engine.cc>


    
class PequodEngine
{
public:
    PequodEngine();
    void SetScene(Scene* scene);
    void sokol_init();
    void sokol_frame_cb();
    void sokol_cleanup();
    void sokol_event(const sapp_event* event);


private:
    uint64_t frame_time;
    double delta_t;
    Scene *currentScene;

};

#endif
