// Hierarchy, Inspector, Project and Console panels of the editor.

#include <algorithm>
#include <format>

#include "editor_scene.h"
#include "misc/cpp/imgui_stdlib.h"
#include "project/asset_database.h"
#include "scripting/type_registry.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

constexpr const char* kNodePayload = "MERCURY_NODE";
constexpr const char* kAssetPayload = "MERCURY_ASSET";

const std::vector<std::string> kImageExtensions = {".png", ".jpg", ".jpeg",
                                                   ".bmp", ".tga"};

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text;
}

// Label shown for an asset in pickers: the file name, with its folder dimmed
// in the tooltip.
std::string AssetLabel(const std::string& res_path) {
  if (res_path.empty()) return "(none)";
  return fs::path(res_path).filename().string();
}

}  // namespace

// -------------------------------------------------------------- Hierarchy ---

void EditorScene::DrawHierarchy() {
  if (!ImGui::Begin("Hierarchy")) {
    ImGui::End();
    return;
  }
  if (ImGui::Button("+ Node")) CreateNode(GetSelectedNode(), "Node");
  ImGui::SetItemTooltip("Add a node under the selection");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", MapDisplayName().c_str());
  ImGui::Separator();

  ImGui::BeginChild("tree");
  DrawHierarchyNode(*map_.GetRoot());

  // Dropping on empty space moves a node to the top level.
  ImGui::Dummy(ImGui::GetContentRegionAvail());
  if (ImGui::BeginDragDropTarget()) {
    if (auto payload = ImGui::AcceptDragDropPayload(kNodePayload)) {
      uint64_t uid = *static_cast<const uint64_t*>(payload->Data);
      if (MapNode* node = map_.FindByUid(uid)) {
        if (map_.ReparentNode(node, map_.GetRoot())) MarkChanged();
      }
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::IsItemClicked()) Select(nullptr);
  if (ImGui::BeginPopupContextItem("empty_space")) {
    if (ImGui::MenuItem("New Node")) CreateNode(nullptr, "Node");
    ImGui::EndPopup();
  }
  ImGui::EndChild();
  ImGui::End();
}

void EditorScene::DrawHierarchyNode(MapNode& node) {
  ImGui::PushID(static_cast<int>(node.uid));
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                             ImGuiTreeNodeFlags_OpenOnDoubleClick |
                             ImGuiTreeNodeFlags_SpanAvailWidth |
                             ImGuiTreeNodeFlags_DefaultOpen;
  if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  if (node.uid == selected_uid_) flags |= ImGuiTreeNodeFlags_Selected;

  bool open = false;
  bool pushed = false;  // whether a TreeNode was pushed and needs TreePop
  if (rename_uid_ == node.uid) {
    // Inline rename field in place of the tree label.
    std::string name = node.name;
    ImGui::SetKeyboardFocusHere();
    if (ImGui::InputText("##rename", &name,
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {
      if (!name.empty() && name != node.name) {
        map_.RenameNode(&node, name);
        MarkChanged();
      }
      rename_uid_ = 0;
    } else if (ImGui::IsItemDeactivated()) {
      rename_uid_ = 0;
    }
    open = !node.children.empty();
  } else {
    open = ImGui::TreeNodeEx("##node", flags, "%s", node.name.c_str());
    pushed = open;
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) Select(&node);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
        node.children.empty()) {
      rename_uid_ = node.uid;
    }

    if (!node.IsRoot() && ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(kNodePayload, &node.uid, sizeof(node.uid));
      ImGui::Text("%s", node.name.c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (auto payload = ImGui::AcceptDragDropPayload(kNodePayload)) {
        uint64_t uid = *static_cast<const uint64_t*>(payload->Data);
        MapNode* dragged = map_.FindByUid(uid);
        if (dragged && map_.ReparentNode(dragged, &node)) MarkChanged();
      }
      ImGui::EndDragDropTarget();
    }
    if (ImGui::BeginPopupContextItem()) {
      Select(&node);
      if (ImGui::MenuItem("New Child")) CreateNode(&node, "Node");
      if (ImGui::MenuItem("Rename", "F2")) rename_uid_ = node.uid;
      if (!node.IsRoot()) {
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelected();
        const int index = node.IndexInParent();
        if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) {
          map_.ReparentNode(&node, node.parent, index - 1);
          MarkChanged();
        }
        if (ImGui::MenuItem("Move Down", nullptr, false,
                            index + 1 < static_cast<int>(node.parent->children.size()))) {
          map_.ReparentNode(&node, node.parent, index + 2);
          MarkChanged();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del")) DeleteSelected();
      }
      ImGui::EndPopup();
    }
  }

  if (open) {
    // Children may be removed while drawing; iterate over a copy of pointers.
    std::vector<MapNode*> children;
    for (auto& child : node.children) children.push_back(child.get());
    for (MapNode* child : children) {
      if (map_.FindByUid(child->uid)) DrawHierarchyNode(*child);
    }
  }
  if (pushed) ImGui::TreePop();
  ImGui::PopID();
}

// -------------------------------------------------------------- Inspector ---

void EditorScene::DrawInspector() {
  if (!ImGui::Begin("Inspector")) {
    ImGui::End();
    return;
  }
  if (!selected_asset_.empty()) {
    DrawAssetInspector(selected_asset_);
  } else if (MapNode* node = GetSelectedNode()) {
    DrawNodeInspector(*node);
  } else {
    ImGui::TextDisabled("Select a node or an asset");
  }
  ImGui::End();
}

void EditorScene::DrawNodeInspector(MapNode& node) {
  auto& registry = TypeRegistry::Instance();

  std::string name = node.name;
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputText("##name", &name, ImGuiInputTextFlags_EnterReturnsTrue) &&
      !name.empty() && name != node.name) {
    map_.RenameNode(&node, name);
    MarkChanged();
  }
  ImGui::TextDisabled("Path: %s", node.GetPath().c_str());
  ImGui::Spacing();

  int remove_index = -1;
  int move_from = -1;
  int move_to = -1;
  for (size_t i = 0; i < node.components.size(); i++) {
    ComponentData& component = node.components[i];
    const ComponentInfo* info = registry.FindComponent(component.type);
    ImGui::PushID(static_cast<int>(i));

    std::string header = component.type;
    if (!info) header += "  (unknown)";
    else if (info->is_script) header += "  (script)";
    const bool open = ImGui::CollapsingHeader(
        (header + "###component").c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    if (info && !info->description.empty()) {
      ImGui::SetItemTooltip("%s", info->description.c_str());
    }
    if (ImGui::BeginPopupContextItem()) {
      const bool removable = !info || info->removable;
      if (ImGui::MenuItem("Remove Component", nullptr, false, removable)) {
        remove_index = static_cast<int>(i);
      }
      if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) {
        move_from = static_cast<int>(i);
        move_to = static_cast<int>(i) - 1;
      }
      if (ImGui::MenuItem("Move Down", nullptr, false,
                          i + 1 < node.components.size())) {
        move_from = static_cast<int>(i);
        move_to = static_cast<int>(i) + 1;
      }
      if (ImGui::MenuItem("Reset to Defaults")) {
        component.properties.clear();
        MarkChanged();
      }
      ImGui::EndPopup();
    }

    if (open) {
      ImGui::Indent(4.0f);
      if (!info) {
        ImGui::TextWrapped(
            "This component type is not known. If it is a script, press "
            "Build. Its values are kept as they are.");
      }
      if (info) {
        for (const auto& field : info->fields) {
          ImGui::PushID(field.name.c_str());
          if (DrawField(field, component.properties)) MarkChanged();
          ImGui::PopID();
        }
      }
      // Properties no field describes (old or misspelled names) stay
      // editable as raw values so nothing is silently lost.
      for (auto& property : component.properties) {
        if (info && info->FindField(property.key)) continue;
        ImGui::PushID(property.key.c_str());
        if (DrawRawProperty(property)) MarkChanged();
        ImGui::PopID();
      }
      component.properties.Remove("");  // removed through the raw editor
      ImGui::Unindent(4.0f);
      ImGui::Spacing();
    }
    ImGui::PopID();
  }

  if (remove_index >= 0) {
    node.components.erase(node.components.begin() + remove_index);
    MarkChanged();
  }
  if (move_from >= 0) {
    std::swap(node.components[move_from], node.components[move_to]);
    MarkChanged();
  }

  ImGui::Spacing();
  const float width = 200.0f;
  ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - width) * 0.5f));
  if (ImGui::Button("Add Component", ImVec2(width, 0))) {
    ImGui::OpenPopup("add_component");
  }
  if (ImGui::BeginPopup("add_component")) {
    static ImGuiTextFilter filter;
    if (ImGui::IsWindowAppearing()) {
      filter.Clear();
      ImGui::SetKeyboardFocusHere();
    }
    filter.Draw("##filter", 220);
    std::string category;
    bool any_scripts = false;
    for (const ComponentInfo* info : registry.Components()) {
      if (node.FindComponent(info->name)) continue;
      if (!filter.PassFilter(info->name.c_str())) continue;
      if (info->category != category) {
        category = info->category;
        ImGui::SeparatorText(category.c_str());
      }
      any_scripts |= info->is_script;
      if (ImGui::MenuItem(info->name.c_str())) {
        node.components.push_back({info->name, {}});
        MarkChanged();
      }
      if (!info->description.empty()) {
        ImGui::SetItemTooltip("%s", info->description.c_str());
      }
    }
    if (!any_scripts) {
      ImGui::SeparatorText("Scripts");
      ImGui::TextDisabled("Build the project to list its scripts");
    }
    ImGui::Separator();
    if (ImGui::MenuItem("New Script...")) {
      modal_ = Modal::kNewScript;
      open_modal_ = true;
    }
    ImGui::EndPopup();
  }
}

bool EditorScene::DrawField(const FieldInfo& field,
                            mtext::PropertyList& properties) {
  const mtext::Variant* stored = properties.Get(field.name);
  mtext::Variant value = stored ? *stored : field.default_value;
  bool changed = false;
  const char* label = field.name.c_str();

  // Label on the left, widget filling the rest, like Unity's Inspector.
  ImGui::AlignTextToFramePadding();
  if (stored && !(*stored == field.default_value)) {
    ImGui::TextUnformatted(label);  // bright when it differs from default
  } else {
    ImGui::TextDisabled("%s", label);
  }
  if (!field.tooltip.empty()) ImGui::SetItemTooltip("%s", field.tooltip.c_str());
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.38f);
  ImGui::SetNextItemWidth(-1);

  switch (field.type) {
    case FieldType::kBool: {
      bool v = value.ToBool().value_or(false);
      if (ImGui::Checkbox("##v", &v)) value = v, changed = true;
      break;
    }
    case FieldType::kInt: {
      int v = static_cast<int>(value.ToInt().value_or(0));
      if (ImGui::DragInt("##v", &v)) value = v, changed = true;
      break;
    }
    case FieldType::kFloat: {
      float v = static_cast<float>(value.ToFloat().value_or(0.0));
      if (ImGui::DragFloat("##v", &v, 0.1f, 0.0f, 0.0f, "%.3g")) {
        value = v, changed = true;
      }
      break;
    }
    case FieldType::kString: {
      std::string v = value.ToString().value_or("");
      if (ImGui::InputText("##v", &v)) value = v, changed = true;
      break;
    }
    case FieldType::kVector2: {
      glm::vec2 v = value.Get<glm::vec2>() ? *value.Get<glm::vec2>() : glm::vec2(0);
      if (ImGui::DragFloat2("##v", &v.x, 0.1f, 0.0f, 0.0f, "%.3g")) {
        value = v, changed = true;
      }
      break;
    }
    case FieldType::kVector3: {
      glm::vec3 v = value.Get<glm::vec3>() ? *value.Get<glm::vec3>() : glm::vec3(0);
      if (ImGui::DragFloat3("##v", &v.x, 0.1f, 0.0f, 0.0f, "%.3g")) {
        value = v, changed = true;
      }
      break;
    }
    case FieldType::kColor: {
      glm::vec4 v = value.Get<mtext::Color>() ? value.Get<mtext::Color>()->rgba
                                               : glm::vec4(1);
      if (ImGui::ColorEdit4("##v", &v.x, ImGuiColorEditFlags_Float)) {
        value = mtext::Color{v}, changed = true;
      }
      break;
    }
    case FieldType::kEnum: {
      std::string current = value.ToString().value_or("");
      if (ImGui::BeginCombo("##v", current.c_str())) {
        for (const auto& option : field.enum_values) {
          if (ImGui::Selectable(option.c_str(), option == current)) {
            value = option;
            changed = option != current;
          }
        }
        ImGui::EndCombo();
      }
      break;
    }
    case FieldType::kNodeRef: {
      std::string path = value.Get<mtext::NodePath>()
                             ? value.Get<mtext::NodePath>()->path
                             : value.ToString().value_or("");
      if (DrawNodePicker("##v", path)) {
        value = mtext::NodePath{path};
        changed = true;
      }
      break;
    }
    case FieldType::kAsset: {
      std::string path = value.Get<mtext::Resource>()
                             ? value.Get<mtext::Resource>()->path
                             : value.ToString().value_or("");
      if (DrawAssetPicker("##v", path, field.asset_type)) {
        value = mtext::Resource{path};
        changed = true;
      }
      break;
    }
  }

  if (ImGui::BeginPopupContextItem("field_menu")) {
    if (ImGui::MenuItem("Reset to Default", nullptr, false, stored != nullptr)) {
      properties.Remove(field.name);
      ImGui::EndPopup();
      return true;
    }
    ImGui::EndPopup();
  }

  if (!changed) return false;
  // Values equal to the default are left out of the file, as Unity does.
  if (value == field.default_value) {
    properties.Remove(field.name);
  } else {
    properties.Set(field.name, value);
  }
  return true;
}

bool EditorScene::DrawRawProperty(mtext::Property& property) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", property.key.c_str());
  ImGui::SetItemTooltip("Not a field of this component. Edit the raw value "
                        "or right-click to remove it.");
  ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.38f);
  ImGui::SetNextItemWidth(-1);
  std::string text = mtext::WriteValue(property.value);
  bool changed = false;
  if (ImGui::InputText("##raw", &text, ImGuiInputTextFlags_EnterReturnsTrue)) {
    if (auto parsed = mtext::ParseValue(text)) {
      property.value = *parsed;
      changed = true;
    } else {
      console_.Error("\"" + text + "\" is not a valid value");
    }
  }
  if (ImGui::BeginPopupContextItem("raw_menu")) {
    if (ImGui::MenuItem("Remove Property")) {
      // Mark for removal by clearing the key; filtered out below.
      property.value = mtext::Variant();
      property.key = "";
      changed = true;
    }
    ImGui::EndPopup();
  }
  return changed;
}

bool EditorScene::DrawNodePicker(const char* label, std::string& path) {
  bool changed = false;
  const std::string preview = path.empty() ? "(none)" : path;
  if (ImGui::BeginCombo(label, preview.c_str())) {
    if (ImGui::Selectable("(none)", path.empty())) {
      path.clear();
      changed = true;
    }
    map_.Visit([&](const MapNode& node) {
      const std::string node_path = node.GetPath();
      if (ImGui::Selectable(node_path.c_str(), node_path == path)) {
        path = node_path;
        changed = true;
      }
    });
    ImGui::EndCombo();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (auto payload = ImGui::AcceptDragDropPayload(kNodePayload)) {
      uint64_t uid = *static_cast<const uint64_t*>(payload->Data);
      if (MapNode* node = map_.FindByUid(uid)) {
        path = node->GetPath();
        changed = true;
      }
    }
    ImGui::EndDragDropTarget();
  }
  return changed;
}

bool EditorScene::DrawAssetPicker(const char* label, std::string& path,
                                  const std::string& asset_type) {
  bool changed = false;
  if (ImGui::BeginCombo(label, AssetLabel(path).c_str())) {
    if (ImGui::Selectable("(none)", path.empty())) {
      path.clear();
      changed = true;
    }
    const bool images = asset_type == "Texture";
    const auto files = images ? ListProjectFiles(kImageExtensions)
                              : ListProjectFiles({std::string(kAssetExtension)});
    for (const auto& file : files) {
      if (!images && !asset_type.empty() && Assets().TypeOf(file) != asset_type) {
        continue;
      }
      if (ImGui::Selectable(file.c_str(), file == path)) {
        path = file;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  if (!path.empty()) ImGui::SetItemTooltip("%s", path.c_str());
  if (ImGui::BeginDragDropTarget()) {
    if (auto payload = ImGui::AcceptDragDropPayload(kAssetPayload)) {
      path = std::string(static_cast<const char*>(payload->Data));
      changed = true;
    }
    ImGui::EndDragDropTarget();
  }
  return changed;
}

void EditorScene::DrawAssetInspector(const std::string& res_path) {
  auto project = GetProject();
  ImGui::TextUnformatted(fs::path(res_path).filename().string().c_str());
  ImGui::TextDisabled("%s", res_path.c_str());
  ImGui::Separator();

  if (!res_path.ends_with(kAssetExtension)) {
    if (ImGui::Button("Open")) OpenInExternalEditor(project->Resolve(res_path));
    return;
  }

  // Assets are small files; edit a fresh copy and write it back on change.
  Asset asset;
  std::string error;
  if (!LoadAssetFile(project->Resolve(res_path), asset, &error)) {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", error.c_str());
    return;
  }
  asset.path = res_path;
  ImGui::Text("Type: %s", asset.type.c_str());
  ImGui::Spacing();

  const AssetTypeInfo* info = TypeRegistry::Instance().FindAssetType(asset.type);
  bool changed = false;
  if (info) {
    for (const auto& field : info->fields) {
      ImGui::PushID(field.name.c_str());
      changed |= DrawField(field, asset.properties);
      ImGui::PopID();
    }
  } else {
    ImGui::TextDisabled("Unknown asset type");
  }
  for (auto& property : asset.properties) {
    if (info && info->FindField(property.key)) continue;
    ImGui::PushID(property.key.c_str());
    changed |= DrawRawProperty(property);
    ImGui::PopID();
  }
  if (changed) {
    asset.properties.Remove("");  // properties removed through the raw editor
    if (Assets().Save(asset)) scene_dirty_ = true;
  }
}

// ---------------------------------------------------------------- Project ---

void EditorScene::DrawProjectBrowser() {
  if (!ImGui::Begin("Project")) {
    ImGui::End();
    return;
  }
  auto project = GetProject();
  if (ImGui::Button("+ Create")) ImGui::OpenPopup("create_menu");
  if (ImGui::BeginPopup("create_menu")) {
    const fs::path dir = project->GetRoot() / "assets";
    if (ImGui::MenuItem("Script...")) {
      modal_ = Modal::kNewScript;
      open_modal_ = true;
    }
    if (ImGui::MenuItem("Map...")) {
      modal_ = Modal::kNewMap;
      open_modal_ = true;
      new_item_dir_ = project->GetRoot() / "maps";
    }
    if (ImGui::MenuItem("Material")) CreateAsset(dir, "Material");
    if (ImGui::MenuItem("Physics Material")) CreateAsset(dir, "PhysicsMaterial");
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", project->GetRoot().string().c_str());
  ImGui::Separator();
  ImGui::BeginChild("files");
  DrawProjectDirectory(project->GetRoot());
  ImGui::EndChild();
  ImGui::End();
}

void EditorScene::DrawProjectDirectory(const fs::path& dir) {
  auto project = GetProject();
  std::vector<fs::directory_entry> folders;
  std::vector<fs::directory_entry> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.starts_with(".")) continue;  // .mercury, .git, ...
    if (entry.is_directory(ec)) {
      if (name == "build" || name.starts_with("cmake-build")) continue;
      folders.push_back(entry);
    } else {
      files.push_back(entry);
    }
  }
  auto by_name = [](const auto& a, const auto& b) {
    return Lower(a.path().filename().string()) < Lower(b.path().filename().string());
  };
  std::sort(folders.begin(), folders.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);

  for (const auto& folder : folders) {
    const std::string name = folder.path().filename().string();
    ImGui::PushID(name.c_str());
    const bool open = ImGui::TreeNodeEx("##dir", ImGuiTreeNodeFlags_SpanAvailWidth,
                                        "%s/", name.c_str());
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("New Folder...")) {
        modal_ = Modal::kNewFolder;
        open_modal_ = true;
        new_item_dir_ = folder.path();
      }
      if (ImGui::MenuItem("New Material")) CreateAsset(folder.path(), "Material");
      if (ImGui::MenuItem("New Physics Material")) {
        CreateAsset(folder.path(), "PhysicsMaterial");
      }
      if (ImGui::MenuItem("New Map...")) {
        modal_ = Modal::kNewMap;
        open_modal_ = true;
        new_item_dir_ = folder.path();
      }
      if (ImGui::MenuItem("Open Folder")) os::OpenWithDefaultApp(folder.path());
      ImGui::Separator();
      if (ImGui::MenuItem("Delete...")) {
        delete_target_ = folder.path();
        modal_ = Modal::kDelete;
        open_modal_ = true;
      }
      ImGui::EndPopup();
    }
    if (open) {
      DrawProjectDirectory(folder.path());
      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  for (const auto& file : files) {
    const fs::path& path = file.path();
    const std::string name = path.filename().string();
    const std::string extension = Lower(path.extension().string());
    const std::string res_path = project->ToResPath(path);
    ImGui::PushID(name.c_str());

    ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (extension == kMapExtension) color = ImVec4(0.55f, 0.8f, 1.0f, 1.0f);
    else if (extension == kAssetExtension) color = ImVec4(0.75f, 0.9f, 0.55f, 1.0f);
    else if (extension == ".cc" || extension == ".h" || extension == ".cpp") {
      color = ImVec4(0.95f, 0.8f, 0.5f, 1.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    const bool selected = res_path == selected_asset_ || res_path == map_path_;
    ImGui::TreeNodeEx("##file",
                      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                          ImGuiTreeNodeFlags_SpanAvailWidth |
                          (selected ? ImGuiTreeNodeFlags_Selected : 0),
                      "%s", name.c_str());
    ImGui::PopStyleColor();

    if (ImGui::BeginDragDropSource()) {
      ImGui::SetDragDropPayload(kAssetPayload, res_path.c_str(), res_path.size() + 1);
      ImGui::TextUnformatted(res_path.c_str());
      ImGui::EndDragDropSource();
    }
    const bool double_clicked =
        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (ImGui::IsItemClicked()) {
      if (extension == kAssetExtension) {
        selected_asset_ = res_path;
        selected_uid_ = 0;
      }
    }
    if (double_clicked) {
      if (extension == kMapExtension) {
        if (res_path != map_path_) {
          if (dirty_) SaveMap();
          OpenMap(res_path);
        }
      } else if (extension != kAssetExtension) {
        OpenInExternalEditor(path);
      }
    }
    if (ImGui::BeginPopupContextItem()) {
      if (extension == kMapExtension && ImGui::MenuItem("Open Map")) {
        if (dirty_) SaveMap();
        OpenMap(res_path);
      }
      if (ImGui::MenuItem("Open in External Editor")) OpenInExternalEditor(path);
      if (ImGui::MenuItem("Copy res:// Path")) ImGui::SetClipboardText(res_path.c_str());
      ImGui::Separator();
      if (ImGui::MenuItem("Delete...", nullptr, false, res_path != map_path_)) {
        delete_target_ = path;
        modal_ = Modal::kDelete;
        open_modal_ = true;
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
}

// ---------------------------------------------------------------- Console ---

void EditorScene::DrawConsole() {
  if (!ImGui::Begin("Console")) {
    ImGui::End();
    return;
  }
  static bool show_info = true;
  static bool show_warnings = true;
  static bool show_errors = true;
  static bool show_game = true;
  static bool show_build = true;
  static ImGuiTextFilter filter;
  static size_t last_version = 0;

  if (ImGui::Button("Clear")) console_.Clear();
  ImGui::SameLine();
  ImGui::Checkbox("Info", &show_info);
  ImGui::SameLine();
  ImGui::Checkbox("Warnings", &show_warnings);
  ImGui::SameLine();
  ImGui::Checkbox("Errors", &show_errors);
  ImGui::SameLine();
  ImGui::Checkbox("Build", &show_build);
  ImGui::SameLine();
  ImGui::Checkbox("Game", &show_game);
  ImGui::SameLine();
  filter.Draw("##filter", -1);
  ImGui::Separator();

  ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_HorizontalScrollbar);
  const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f;
  for (const auto& line : console_.Snapshot()) {
    if (line.level == Console::Level::kInfo && !show_info) continue;
    if (line.level == Console::Level::kWarning && !show_warnings) continue;
    if (line.level == Console::Level::kError && !show_errors) continue;
    if (line.source == Console::Source::kGame && !show_game) continue;
    if (line.source == Console::Source::kBuild && !show_build) continue;
    if (!filter.PassFilter(line.text.c_str())) continue;
    ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
    if (line.level == Console::Level::kWarning) color = ImVec4(1.0f, 0.8f, 0.35f, 1.0f);
    if (line.level == Console::Level::kError) color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
    const char* tag = line.source == Console::Source::kGame    ? "[game]  "
                      : line.source == Console::Source::kBuild ? "[build] "
                                                               : "[editor]";
    ImGui::TextDisabled("%s", tag);
    ImGui::SameLine();
    ImGui::TextColored(color, "%s", line.text.c_str());
  }
  // Follow new output unless the user scrolled up to read.
  const size_t version = console_.GetVersion();
  if (version != last_version && at_bottom) ImGui::SetScrollHereY(1.0f);
  last_version = version;
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace Mercury
