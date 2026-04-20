//
// Created by smadhur on 3/31/26.
//

#include "object_properties.h"

#include <format>

#include "imgui/imgui.h"
#include "pobject/pobject.h"

namespace Pequod {
ObjectPropertiesPanel::ObjectPropertiesPanel(
    entity_id &selected_id, std::shared_ptr<PObjectManager> manager)
    : selected_id(selected_id), Panel(true) {
  this->manager = manager;
}

void ObjectPropertiesPanel::Initialize() {}

void ObjectPropertiesPanel::Draw() {
  if (selected_id != 0 && prev_id != selected_id) {
    prev_id = selected_id;
    auto obj = manager->GetObjectById(selected_id);
    if (obj) {
      strcpy(input_name, obj->name.c_str());
      auto pos = obj->Get<Position>();
      if (pos) position = pos->Get();
      auto mesh = obj->Get<Mesh>();
      if (mesh) scale = mesh->GetScale();
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
  auto obj = manager->GetObjectById(id);
  if (!obj) return;

  {
    // Object info
    ImGui::SeparatorText("Object Info");
    if (ImGui::InputText("Name", input_name, 255,
                         ImGuiInputTextFlags_CharsNoBlank |
                             ImGuiInputTextFlags_EnterReturnsTrue) &&
        strlen(input_name) > 2) {
      obj->name = input_name;
    }
  }
  {
    // Position
    auto pos = obj->Get<Position>();
    if (pos) {
      ImGui::SeparatorText("Position");
      ImGui::DragFloat("x###posx", &position.x);
      ImGui::DragFloat("y###posy", &position.y);
      ImGui::DragFloat("z###posz", &position.z);
      pos->Set(position);
    }
  }
  {
    // Mesh scale
    auto mesh = obj->Get<Mesh>();
    if (mesh) {
      ImGui::SeparatorText("Size");
      ImGui::DragFloat("x###sizex", &scale.x, 1, 0, 99999);
      ImGui::DragFloat("y###sizey", &scale.y, 1, 0, 99999);
      ImGui::DragFloat("z###sizez", &scale.z, 1, 0, 99999);
      mesh->SetScale(scale);
    }
  }
}
}  // namespace Pequod
