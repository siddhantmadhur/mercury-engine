#include "editor_scene.hh"


namespace Pequod {

EditorScene::EditorScene() {}

void EditorScene::OnDestroyInternal() {
    this->OnDestroy();
}

void EditorScene::InitializeScene() {
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
    OnFrameUpdate();
}

bool EditorScene::IsKeyPressed(sapp_keycode keycode) {
    if (this->pressed_keys.contains(keycode)) {
        return pressed_keys[keycode];
    } 
    return false;
}

}
