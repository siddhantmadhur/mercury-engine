//
// Created by smadhur on 3/30/26.
//

#include "object_tree.h"

#include <format>

#include "imgui/imgui.h"
#include "pobject/pobject.h"

namespace Pequod {
    ObjectTree::ObjectTree(const fs::path& dir, std::shared_ptr<ECS> ecs, entity_id& selected_id)
    : selected_id(selected_id), Panel(true) {
        this->ecs = ecs;
        this->project_path = dir;
    };
    ObjectTree::~ObjectTree() = default;

    void ObjectTree::Initialize() {

    }

    void ObjectTree::DrawID(entity_id id) {
        ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Leaf;
        if (id != 0 && id == selected_id) {
            base_flags |= ImGuiTreeNodeFlags_Selected;
        }
        auto obj = ecs->GetProperty<PObject>(id);
        if (!obj) {
            return;
        }
        auto arr = obj->children;
        if (ImGui::TreeNodeEx(id == 0 ? "root" : std::format("{}[{}]", obj->name, id).c_str(), base_flags)) {
            if (ImGui::IsItemClicked()) {
                selected_id = selected_id == id ? 0 : id;
            }
            for (int i = 0 ; i < arr.size(); i++) {
                auto cur = arr[i];
                DrawID(cur);
            }
            ImGui::TreePop();
        }
    }
    void ObjectTree::Draw() {
        ImGui::Begin("ObjectTree");
        if (ecs) {
            if (ImGui::IsWindowHovered() && !ImGui::IsItemClicked() && ImGui::IsMouseClicked(0)) {
                selected_id = 0;
            }
            ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen;
            DrawID(0);
        }
        ImGui::End();
    }
}
