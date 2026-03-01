#ifndef MERCURY_ENGINE_IMPL_HH
#define MERCURY_ENGINE_IMPL_HH


#include <condition_variable>
#include <cstdint>
#include <engine/scene.hh>

#include <sokol/sokol_app.h>
#include <sokol/sokol_gfx.h>
#include <thread>


//#include <engine/engine.cc>

class PequodEngine
{
public:
    PequodEngine();
    void startUp();
    void SetScene(Scene* scene);
    void sokol_init();
    void sokol_frame_cb();
    void sokol_cleanup();
    void sokol_event(const sapp_event* event);
    uint64_t getTicks();
    uint64_t ticks;
    std::condition_variable ticks_cv;
    bool isShowDebugStats();
private:
    uint64_t frame_time;
    double delta_t;
    uint8_t fps;
    double total_t;
    Scene *currentScene;
    std::thread ticks_thread;
    bool show_debug_stats = false;
};

#endif
