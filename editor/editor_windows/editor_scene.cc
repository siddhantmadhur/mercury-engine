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
    OnEventUpdate(event);
}
void EditorScene::OnFrameInternal() {
    OnFrameUpdate();
}

}
