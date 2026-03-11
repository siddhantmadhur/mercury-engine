#include "editor_project.hh"
#include "editor_scene.hh"
#include "imgui/imgui.h"


namespace Pequod {

void ProjectSelectionScene::OnInitialLoad() {}
void ProjectSelectionScene::OnEventUpdate(const sapp_event* event) {
    if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
        if (event->key_code == SAPP_KEYCODE_ESCAPE) {
            sapp_quit();
        }
    }
}

void ProjectSelectionScene::OnFrameUpdate() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit", "Esc")) {
                sapp_quit();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void ProjectSelectionScene::OnTickUpdate() {}
void ProjectSelectionScene::OnDestroy() {}

}
