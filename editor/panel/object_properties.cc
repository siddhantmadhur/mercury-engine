//
// Created by smadhur on 3/31/26.
//

#include "object_properties.h"

#include <format>

#include "imgui/imgui.h"
#include "pobject/pobject.h"

namespace Pequod {
    ObjectPropertiesPanel::ObjectPropertiesPanel(entity_id& selected_id, std::shared_ptr<ECS> ecs)
    : selected_id(selected_id), Panel(true) {
        this->ecs = ecs;
    }

    void ObjectPropertiesPanel::Initialize() {}
    void ObjectPropertiesPanel::Draw() {
        if (selected_id != 0 && prev_id != selected_id) {
            PDebug::info(std::format("Selected new entity: {}", selected_id));
            prev_id = selected_id;
            auto obj = ecs->GetProperty<PObject>(selected_id) ;

            if (obj && selected_id != 0) {
                strcpy(input_name, obj->name.c_str());
            }
            auto pos = ecs->GetProperty<Position>(selected_id);
            if (pos) {
                position = pos->position;
            }
        }
        ImGui::Begin("ObjectProperties");
        if (selected_id != 0) {
            DrawProperties(selected_id);
        } else {
            ImGui::Text("No object selected");
        }
        ImGui::End();
    }

    void ObjectPropertiesPanel::DrawProperties(uint64_t id) {
        { // Object info
            auto obj_info = ecs->GetProperty<PObject>(id);
            if (obj_info) {
                ImGui::SeparatorText("Object Info");
                if (ImGui::InputText(
                    "Name", input_name, 255,
                    ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue) && strlen(input_name) > 2) {
                    obj_info->name = input_name;
                }
            }
        }
        { // Position info
            auto pos = ecs->GetProperty<Position>(id);
            if (pos) {
                ImGui::SeparatorText("Position");
                if (ImGui::DragFloat("x", &position.x)) {
                    //ecs->SetPosition(id, position);
                    pos->position = position;
                    pos->raw_position = position;
                }
            }
        }
    }
}
