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
    void initializeJolt(); // run this in the main thread
    void startUp();
    void SetScene(Scene* scene);
    void sokol_init();
    void sokol_frame_cb();
    void sokol_cleanup();
    void sokol_event(const sapp_event* event);
    uint64_t getTicks();
    std::condition_variable ticks_cv;
    bool isShowDebugStats();
private:
    uint64_t ticks;
    uint64_t last_tick;
    uint64_t frame_time;
    uint64_t tick_frame_time;
    double tick_t;
    double delta_t;
    uint8_t fps;
    double total_t;
    Scene *currentScene;
    bool show_debug_stats = false;
};

#endif
