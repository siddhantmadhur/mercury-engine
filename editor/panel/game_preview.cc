#include "game_preview.hh"
#include "imgui/imgui.h"


namespace Pequod {


GamePreview::GamePreview() {
    
}

void GamePreview::Initialize() {
    //gameScene = new BreakoutScene();
    //gameScene->Init();
    //gameScene->OnStart();     
}

void GamePreview::Draw() {
    if (show_panel) {
        ImGui::Begin("GamePreview");
        auto * dl = ImGui::GetWindowDrawList();
        //gameScene->Render(1280, 720);
        ImGui::End();
    }
}

} // namespace Pequod
