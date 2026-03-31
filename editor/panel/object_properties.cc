//
// Created by smadhur on 3/31/26.
//

#include "object_properties.h"

#include "imgui/imgui.h"

namespace Pequod {
    ObjectPropertiesPanel::ObjectPropertiesPanel(entity_id& selected_id, std::shared_ptr<ECS> ecs)
    : selected_id(selected_id), Panel(true) {
        this->ecs = ecs;
    }

    void ObjectPropertiesPanel::Initialize() {}
    void ObjectPropertiesPanel::Draw() {
        ImGui::Begin("ObjectProperties");
        if (selected_id != 0) {

        } else {
            ImGui::Text("No object selected");
        }
        ImGui::End();
    }
}
