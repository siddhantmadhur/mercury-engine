#ifndef EDITOR_SCENE_IMPL_HH_
#define EDITOR_SCENE_IMPL_HH_

#include "engine/world_scene.h"
#include "panel/game_preview.hh"
#include <cstdint>
#include <sokol/sokol_app.h>
#include <unordered_map>
#include <vector>

namespace Pequod {
class EditorScene {
public:
    EditorScene(); 
    bool IsKeyPressed(sapp_keycode);

    /**
    * Extend these functions to create functionality in scenes
    **/
    virtual void OnInitialLoad() =0;
    virtual void OnEventUpdate(const sapp_event* event) =0;
    virtual void OnFrameUpdate() =0;
    virtual void OnTickUpdate() =0;
    virtual void OnDestroy()=0;

    /**
    * These functions are meant for the engine to interface with scenes directly.
    * DO NOT EXTEND THESE FUNCTIONS
    **/
    void InitializeScene();
    void OnEventInternal(const sapp_event* event);     
    void OnFrameInternal();
    void OnDestroyInternal();
    virtual void RenderScenePreview(WorldScene** scene) =0;
    
    //BreakoutScene *game_scene;

protected:
    uint64_t current_tick;

private:
    std::unordered_map<sapp_keycode, bool> pressed_keys;
    float total_run_time=0.0f;
    uint64_t frame_time = 1;
    float tick_frame_time = 1;
    float delta_t = 0;
};
}

#endif
