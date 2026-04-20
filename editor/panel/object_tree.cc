//
// Created by smadhur on 3/30/26.
//

#include "object_tree.h"

#include <format>

#include "imgui/imgui.h"
#include "pobject/pobject.h"

namespace Pequod {
ObjectTree::ObjectTree(const fs::path &dir,
                       std::shared_ptr<PObjectManager> manager,
                       entity_id &selected_id)
    : selected_id(selected_id), Panel(true) {
  this->manager = manager;
  this->project_path = dir;
};

ObjectTree::~ObjectTree() = default;

void ObjectTree::Initialize() {}

void ObjectTree::DrawID(entity_id id) {
  auto obj = manager->GetObjectById(id);
  if (!obj) return;

  ImGuiTreeNodeFlags flags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
  if (obj->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  if (id != 0 && id == selected_id) flags |= ImGuiTreeNodeFlags_Selected;

  bool open = ImGui::TreeNodeEx(
      id == 0 ? "root" : std::format("{}###{}", obj->name, id).c_str(), flags);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    selected_id = (selected_id == id) ? 0 : id;
  }
  if (open) {
    for (auto child_id : obj->children) {
      DrawID(child_id);
    }
    ImGui::TreePop();
  }
}

void ObjectTree::Draw() {
  ImGui::Begin("ObjectTree");
  if (manager) {
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(0)) {
      selected_id = 0;
    }
    DrawID(0);
  }
  ImGui::End();
}
}  // namespace Pequod
