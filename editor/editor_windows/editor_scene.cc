#include "editor_scene.hh"


namespace Pequod {

EditorScene::EditorScene() {}


void EditorScene::InitializeScene() {}
void EditorScene::OnEventInternal(const sapp_event* event) {
    OnEventUpdate(event);
}
void EditorScene::OnFrameInternal() {
    OnFrameUpdate();
}

}
