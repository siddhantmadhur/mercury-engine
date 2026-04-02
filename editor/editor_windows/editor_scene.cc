#include "editor_scene.hh"

#include <sokol/sokol_time.h>


namespace Pequod {

EditorScene::EditorScene() {
}

void EditorScene::OnDestroyInternal() {
    this->OnDestroy();
}

void EditorScene::InitializeScene() {

    //game_scene = new BreakoutScene();
    //game_scene->Init();
    //game_scene->OnStart();

    this->OnInitialLoad();
}
void EditorScene::OnEventInternal(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        pressed_keys[event->key_code] = true;
    }
    if (event->type == SAPP_EVENTTYPE_KEY_UP) {
        pressed_keys[event->key_code] = false;
    }

    OnEventUpdate(event);
}
void EditorScene::OnFrameInternal() {


    delta_t = stm_ms(stm_laptime(&frame_time));
    total_run_time += delta_t;


    uint64_t ticks = int(total_run_time / (1000.0/60.0)); // 60 ticks per second

    if (ticks > current_tick) {
        current_tick = ticks;
        OnTickUpdate();
    }

    OnFrameUpdate();
}


bool EditorScene::IsKeyPressed(sapp_keycode keycode) {
    if (this->pressed_keys.contains(keycode)) {
        return pressed_keys[keycode];
    } 
    return false;
}

}
