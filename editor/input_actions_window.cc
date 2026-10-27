#include "input_actions_window.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>

#include "GLFW/glfw3.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "input/input_actions_codegen.h"
#include "os/filesystem.h"
#include "templates.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxUndoSteps = 200;
constexpr const char* kPartNames[] = {"Up", "Down", "Left", "Right"};

std::vector<std::string>& Part(InputActionDesc::Composite2D& composite, int part) {
  switch (part) {
    case 0:
      return composite.up;
    case 1:
      return composite.down;
    case 2:
      return composite.left;
    default:
      return composite.right;
  }
}

// A column title with a "+" button at its right edge; true when pressed.
bool HeaderWithAddButton(const char* title, const char* id, const char* tooltip) {
  const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(title);
  // SameLine(x) would measure from the table column; SetCursorPosX does not.
  ImGui::SameLine();
  ImGui::SetCursorPosX(right - ImGui::GetFrameHeight());
  const bool pressed = ImGui::Button(id, ImVec2(ImGui::GetFrameHeight(), 0));
  ImGui::SetItemTooltip("%s", tooltip);
  ImGui::Separator();
  return pressed;
}

std::string BindingLabel(const std::string& path) {
  return path.empty() ? "<No Binding>" : path;
}

std::string PartLabel(const std::vector<std::string>& paths) {
  if (paths.empty()) return "<No Binding>";
  std::string label;
  for (const auto& path : paths) label += (label.empty() ? "" : ", ") + path;
  return label;
}

// Writes `text` unless the file already holds it, so a save that changes
// nothing in the header does not rebuild every script that includes it.
bool WriteIfChanged(const fs::path& file, const std::string& text) {
  if (auto current = os::ReadText(file); current && *current == text) return true;
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path temp = file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary);
    if (!out || !(out << text)) return false;
  }
  fs::rename(temp, file, ec);
  return !ec;
}

}  // namespace

InputActionsWindow::InputActionsWindow(Console& console) : console_(console) {}

void InputActionsWindow::SetProject(std::shared_ptr<Project> project) {
  project_ = std::move(project);
  Close();
}

// ---------------------------------------------------------------- Document ---

bool InputActionsWindow::Open(const std::string& res_path) {
  if (!project_) return false;
  auto text = os::ReadText(project_->Resolve(res_path));
  if (!text) {
    console_.Error("Input Actions: could not read " + res_path);
    return false;
  }
  InputActionsDesc desc;
  std::string error;
  if (!ParseInputActions(*text, &desc, &error)) {
    console_.Error(std::format("{}: {}", res_path, error));
    return false;
  }
  desc_ = std::move(desc);
  path_ = res_path;
  dirty_ = false;
  map_ = 0;
  selection_ = {};
  undo_.clear();
  redo_.clear();
  header_errors_.clear();
  renaming_map_ = -1;
  listen_id_ = 0;
  return true;
}

void InputActionsWindow::Close() {
  path_.clear();
  desc_ = {};
  dirty_ = false;
  selection_ = {};
  undo_.clear();
  redo_.clear();
  header_errors_.clear();
  listen_id_ = 0;
}

bool InputActionsWindow::Save() {
  if (!project_ || path_.empty()) return false;
  if (!SaveInputActions(project_->Resolve(path_), desc_)) {
    console_.Error("Input Actions: could not write " + path_);
    return false;
  }
  dirty_ = false;
  if (!desc_.header.empty() && !WriteHeader()) return false;
  console_.Info("Saved " + path_);
  return true;
}

bool InputActionsWindow::WriteHeader() {
  const std::string class_name = desc_.class_name.empty() ? "GameInput" : desc_.class_name;
  auto header = GenerateInputActionsHeader(desc_, class_name, path_);
  header_errors_ = std::move(header.errors);
  if (!header_errors_.empty()) {
    for (const auto& e : header_errors_) console_.Error("Input Actions: " + e);
    console_.Error("Input Actions: " + desc_.header + " was not written; rename the actions");
    return false;
  }
  if (!WriteIfChanged(project_->Resolve(desc_.header), header.source)) {
    console_.Error("Input Actions: could not write " + desc_.header);
    return false;
  }
  return true;
}

void InputActionsWindow::Checkpoint() {
  undo_.push_back({desc_, map_, selection_});
  if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
  redo_.clear();
}

void InputActionsWindow::MarkChanged() { dirty_ = true; }

void InputActionsWindow::Undo() {
  if (undo_.empty()) return;
  redo_.push_back({desc_, map_, selection_});
  desc_ = std::move(undo_.back().desc);
  map_ = undo_.back().map;
  selection_ = undo_.back().selection;
  undo_.pop_back();
  dirty_ = true;
}

void InputActionsWindow::Redo() {
  if (redo_.empty()) return;
  undo_.push_back({desc_, map_, selection_});
  desc_ = std::move(redo_.back().desc);
  map_ = redo_.back().map;
  selection_ = redo_.back().selection;
  redo_.pop_back();
  dirty_ = true;
}

const std::string& InputActionsWindow::SelectedMap() const {
  static const std::string kNone;
  if (map_ < 0 || map_ >= static_cast<int>(desc_.maps.size())) return kNone;
  return desc_.maps[map_];
}

std::vector<int> InputActionsWindow::ActionsOf(const std::string& map) const {
  std::vector<int> result;
  for (int i = 0; i < static_cast<int>(desc_.actions.size()); i++) {
    if (desc_.actions[i].map == map) result.push_back(i);
  }
  return result;
}

std::string InputActionsWindow::UniqueActionName(const std::string& map,
                                                 std::string base) const {
  auto taken = [&](const std::string& name) {
    return std::any_of(desc_.actions.begin(), desc_.actions.end(), [&](const auto& a) {
      return a.map == map && a.name == name;
    });
  };
  std::string name = base;
  for (int i = 1; taken(name); i++) name = base + std::to_string(i);
  return name;
}

std::string InputActionsWindow::UniqueMapName(std::string base) const {
  std::string name = base;
  for (int i = 1; std::find(desc_.maps.begin(), desc_.maps.end(), name) != desc_.maps.end();
       i++) {
    name = base + std::to_string(i);
  }
  return name;
}

void InputActionsWindow::AddMap() {
  Checkpoint();
  desc_.maps.push_back(UniqueMapName("New action map"));
  map_ = static_cast<int>(desc_.maps.size()) - 1;
  selection_ = {};
  renaming_map_ = map_;
  rename_buffer_ = desc_.maps[map_];
  focus_rename_ = true;
  MarkChanged();
}

void InputActionsWindow::RenameMap(const std::string& from, const std::string& to) {
  if (to.empty() || to == from || to.find('/') != std::string::npos ||
      std::find(desc_.maps.begin(), desc_.maps.end(), to) != desc_.maps.end()) {
    return;
  }
  Checkpoint();
  for (auto& map : desc_.maps) {
    if (map == from) map = to;
  }
  for (auto& action : desc_.actions) {
    if (action.map == from) action.map = to;
  }
  MarkChanged();
}

void InputActionsWindow::DeleteMap(const std::string& map) {
  Checkpoint();
  std::erase(desc_.maps, map);
  std::erase_if(desc_.actions, [&](const auto& a) { return a.map == map; });
  map_ = std::clamp(map_, 0, std::max(0, static_cast<int>(desc_.maps.size()) - 1));
  selection_ = {};
  MarkChanged();
}

void InputActionsWindow::AddAction(const std::string& map) {
  Checkpoint();
  InputActionDesc action;
  action.map = map;
  action.name = UniqueActionName(map, "New action");
  action.bindings.emplace_back();
  // Keep the map's actions together, so the file reads in window order.
  const auto actions = ActionsOf(map);
  const int index = actions.empty() ? static_cast<int>(desc_.actions.size()) : actions.back() + 1;
  desc_.actions.insert(desc_.actions.begin() + index, std::move(action));
  selection_ = {Selection::Kind::kAction, index};
  MarkChanged();
}

void InputActionsWindow::DeleteSelected() {
  if (selection_.action < 0 || selection_.action >= static_cast<int>(desc_.actions.size())) {
    return;
  }
  auto& action = desc_.actions[selection_.action];
  Checkpoint();
  switch (selection_.kind) {
    case Selection::Kind::kAction:
      desc_.actions.erase(desc_.actions.begin() + selection_.action);
      selection_ = {};
      break;
    case Selection::Kind::kBinding:
      action.bindings.erase(action.bindings.begin() + selection_.index);
      selection_ = {Selection::Kind::kAction, selection_.action};
      break;
    case Selection::Kind::kComposite:
      action.composites.erase(action.composites.begin() + selection_.index);
      selection_ = {Selection::Kind::kAction, selection_.action};
      break;
    case Selection::Kind::kNone:
      return;
  }
  MarkChanged();
}

void InputActionsWindow::DuplicateSelected() {
  if (selection_.action < 0 || selection_.action >= static_cast<int>(desc_.actions.size())) {
    return;
  }
  Checkpoint();
  auto& action = desc_.actions[selection_.action];
  switch (selection_.kind) {
    case Selection::Kind::kAction: {
      InputActionDesc copy = action;
      copy.name = UniqueActionName(copy.map, copy.name);
      desc_.actions.insert(desc_.actions.begin() + selection_.action + 1, std::move(copy));
      selection_.action++;
      break;
    }
    case Selection::Kind::kBinding:
      action.bindings.insert(action.bindings.begin() + selection_.index + 1,
                             action.bindings[selection_.index]);
      selection_.index++;
      break;
    case Selection::Kind::kComposite:
      action.composites.insert(action.composites.begin() + selection_.index + 1,
                               action.composites[selection_.index]);
      selection_.index++;
      break;
    case Selection::Kind::kNone:
      return;
  }
  MarkChanged();
}

// ------------------------------------------------------------------ Listen ---

void InputActionsWindow::CaptureListen(const InputManager* input) {
  if (!listen_id_ || !input) return;
  for (const auto& event : input->GetFrameEvents()) {
    if (event.type != InputEvent::Type::kKey || event.action != GLFW_PRESS) continue;
    if (event.key == GLFW_KEY_ESCAPE) {
      listen_id_ = 0;
      return;
    }
    if (std::string control = ControlFromKey(event.key); !control.empty()) {
      listened_ = "<Keyboard>/" + control;
      return;
    }
  }
  const GamepadState& pad = input->GetGamepad();
  if (!pad.connected) return;
  for (int button = 0; button < 15; button++) {
    // Only a press that starts while listening counts.
    if (pad.buttons[button] && !listen_pad_[button]) {
      if (std::string control = ControlFromGamepadButton(button); !control.empty()) {
        listened_ = "<Gamepad>/" + control;
        return;
      }
    }
    listen_pad_[button] = pad.buttons[button];
  }
  if (pad.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.5f) listened_ = "<Gamepad>/leftTrigger";
  if (pad.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.5f) listened_ = "<Gamepad>/rightTrigger";
}

// -------------------------------------------------------------------- Draw ---

void InputActionsWindow::Draw(const InputManager* input, bool* open) {
  ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);
  // The title stays the window's id; the dot marks unsaved changes.
  const ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_MenuBar | (dirty_ ? ImGuiWindowFlags_UnsavedDocument : 0);
  if (!ImGui::Begin(kWindowName, open, window_flags)) {
    has_focus_ = false;
    listen_id_ = 0;
    ImGui::End();
    return;
  }
  has_focus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  CaptureListen(input);
  DrawToolbar();

  if (!project_) {
    ImGui::TextDisabled("Open a project to edit its input actions.");
    ImGui::End();
    return;
  }
  if (path_.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("Open a .minput file: double-click it in the Project panel,");
    ImGui::TextDisabled("or create one with Project > + Create > Input Actions.");
    ImGui::End();
    return;
  }

  // Delete / Ctrl+D act on the selection, unless a text field has the keys.
  if (has_focus_ && !ImGui::GetIO().WantTextInput && !listen_id_) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelected();
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
  if (ImGui::BeginTable("input_actions_columns", 3, flags)) {
    ImGui::TableSetupColumn("Action Maps", ImGuiTableColumnFlags_WidthStretch, 0.22f);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch, 0.40f);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    DrawMaps();
    ImGui::TableSetColumnIndex(1);
    DrawActions();
    ImGui::TableSetColumnIndex(2);
    DrawProperties();
    ImGui::EndTable();
  }
  ImGui::End();
}

void InputActionsWindow::DrawToolbar() {
  if (!ImGui::BeginMenuBar()) return;
  const bool open = project_ && !path_.empty();
  if (ImGui::MenuItem("Save", "Ctrl+S", false, open && dirty_)) Save();
  if (ImGui::BeginMenu("Code", open)) {
    bool generate = !desc_.header.empty();
    if (ImGui::Checkbox("Generate C++ Header", &generate)) {
      Checkpoint();
      if (generate) {
        if (desc_.class_name.empty()) desc_.class_name = "GameInput";
        desc_.header = "res://scripts/" + Templates::ToSnakeCase(desc_.class_name) + ".h";
      } else {
        desc_.header.clear();
      }
      MarkChanged();
    }
    ImGui::SetItemTooltip("Write a header with a struct per action map when saving,\n"
                          "like Unity's Generate C# Class");
    ImGui::BeginDisabled(!generate);
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputText("Class Name", &desc_.class_name)) MarkChanged();
    if (ImGui::IsItemActivated()) Checkpoint();
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputText("Header", &desc_.header)) MarkChanged();
    if (ImGui::IsItemActivated()) Checkpoint();
    if (ImGui::MenuItem("Generate Now")) WriteHeader();
    ImGui::EndDisabled();
    ImGui::EndMenu();
  }
  if (open) {
    if (project_->GetInputActionsPath() == path_) {
      ImGui::TextDisabled("Project Actions");
      ImGui::SetItemTooltip("The game loads these actions ([input] in the .mproj)");
    } else {
      if (ImGui::MenuItem("Use as Project Actions")) {
        project_->SetInputActionsPath(path_);
        project_->Save();
        console_.Info(path_ + " is now the project's input actions");
      }
      ImGui::SetItemTooltip("Load these actions in the game ([input] in the .mproj)");
    }
    ImGui::TextDisabled("%s", path_.c_str());
  }
  ImGui::EndMenuBar();
}

void InputActionsWindow::DrawMaps() {
  if (HeaderWithAddButton("Action Maps", "+##map", "Add Action Map")) AddMap();

  ImGui::BeginChild("maps");
  std::optional<std::string> delete_map;
  for (int i = 0; i < static_cast<int>(desc_.maps.size()); i++) {
    const std::string map = desc_.maps[i];
    ImGui::PushID(i);
    if (renaming_map_ == i) {
      if (focus_rename_) {
        ImGui::SetKeyboardFocusHere();
        focus_rename_ = false;
      }
      ImGui::SetNextItemWidth(-1);
      const bool done = ImGui::InputText("##rename", &rename_buffer_,
                                         ImGuiInputTextFlags_EnterReturnsTrue |
                                             ImGuiInputTextFlags_AutoSelectAll);
      if (done || ImGui::IsItemDeactivated()) {
        RenameMap(map, rename_buffer_);
        renaming_map_ = -1;
      }
    } else {
      if (ImGui::Selectable(map.empty() ? "(no map)" : map.c_str(), map_ == i,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        if (map_ != i) selection_ = {};
        map_ = i;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          renaming_map_ = i;
          rename_buffer_ = map;
          focus_rename_ = true;
        }
      }
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Rename")) {
          renaming_map_ = i;
          rename_buffer_ = map;
          focus_rename_ = true;
        }
        if (ImGui::MenuItem("Add Action")) {
          map_ = i;
          AddAction(map);
        }
        if (ImGui::MenuItem("Delete")) delete_map = map;
        ImGui::EndPopup();
      }
    }
    ImGui::PopID();
  }
  if (desc_.maps.empty()) ImGui::TextDisabled("No action maps. Press + to add one.");
  ImGui::EndChild();
  if (delete_map) DeleteMap(*delete_map);
}

void InputActionsWindow::DrawActions() {
  const std::string& map = SelectedMap();
  const bool has_map = !desc_.maps.empty();
  ImGui::BeginDisabled(!has_map);
  if (HeaderWithAddButton("Actions", "+##action", "Add Action")) AddAction(map);
  ImGui::EndDisabled();

  ImGui::BeginChild("actions");
  if (!has_map) {
    ImGui::TextDisabled("Add an action map first.");
    ImGui::EndChild();
    return;
  }
  // Context menus may change the list, so they run after it is drawn.
  std::function<void()> pending;
  for (int index : ActionsOf(map)) {
    auto& action = desc_.actions[index];
    ImGui::PushID(index);
    const bool action_selected =
        selection_.kind == Selection::Kind::kAction && selection_.action == index;
    const bool open = ImGui::TreeNodeEx(
        "##action",
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
            ImGuiTreeNodeFlags_OpenOnArrow | (action_selected ? ImGuiTreeNodeFlags_Selected : 0),
        "%s", action.name.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
      selection_ = {Selection::Kind::kAction, index};
    }
    if (ImGui::BeginPopupContextItem()) {
      selection_ = {Selection::Kind::kAction, index};
      DrawActionContextMenu(index);
      ImGui::EndPopup();
    }
    if (open) {
      for (int b = 0; b < static_cast<int>(action.bindings.size()); b++) {
        ImGui::PushID(b);
        const bool selected = selection_ == Selection{Selection::Kind::kBinding, index, b};
        ImGui::TreeNodeEx("##binding",
                          ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                              ImGuiTreeNodeFlags_SpanAvailWidth |
                              (selected ? ImGuiTreeNodeFlags_Selected : 0),
                          "%s", BindingLabel(action.bindings[b]).c_str());
        if (ImGui::IsItemClicked()) selection_ = {Selection::Kind::kBinding, index, b};
        if (ImGui::BeginPopupContextItem()) {
          selection_ = {Selection::Kind::kBinding, index, b};
          if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending = [this] { DuplicateSelected(); };
          if (ImGui::MenuItem("Delete", "Del")) pending = [this] { DeleteSelected(); };
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      for (int c = 0; c < static_cast<int>(action.composites.size()); c++) {
        ImGui::PushID(1000 + c);
        const bool selected = selection_ == Selection{Selection::Kind::kComposite, index, c};
        const bool composite_open = ImGui::TreeNodeEx(
            "##composite",
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth |
                ImGuiTreeNodeFlags_OpenOnArrow | (selected ? ImGuiTreeNodeFlags_Selected : 0),
            "2D Vector");
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
          selection_ = {Selection::Kind::kComposite, index, c};
        }
        if (ImGui::BeginPopupContextItem()) {
          selection_ = {Selection::Kind::kComposite, index, c};
          if (ImGui::MenuItem("Duplicate", "Ctrl+D")) pending = [this] { DuplicateSelected(); };
          if (ImGui::MenuItem("Delete", "Del")) pending = [this] { DeleteSelected(); };
          ImGui::EndPopup();
        }
        if (composite_open) {
          for (int part = 0; part < 4; part++) {
            ImGui::TreeNodeEx(kPartNames[part],
                              ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth,
                              "%s: %s", kPartNames[part],
                              PartLabel(Part(action.composites[c], part)).c_str());
            if (ImGui::IsItemClicked()) selection_ = {Selection::Kind::kComposite, index, c};
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (ActionsOf(map).empty()) ImGui::TextDisabled("No actions. Press + to add one.");
  ImGui::EndChild();
  if (pending) pending();
}

void InputActionsWindow::DrawActionContextMenu(int index) {
  auto& action = desc_.actions[index];
  if (ImGui::MenuItem("Add Binding")) {
    Checkpoint();
    action.bindings.emplace_back();
    selection_ = {Selection::Kind::kBinding, index,
                  static_cast<int>(action.bindings.size()) - 1};
    MarkChanged();
  }
  if (ImGui::MenuItem("Add Up/Down/Left/Right Composite")) {
    Checkpoint();
    action.composites.emplace_back();
    // A 2D composite reads a Vector2.
    if (action.type == InputAction::Type::kButton) action.type = InputAction::Type::kValue;
    action.vector2 = true;
    selection_ = {Selection::Kind::kComposite, index,
                  static_cast<int>(action.composites.size()) - 1};
    MarkChanged();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelected();
  if (ImGui::MenuItem("Delete", "Del")) DeleteSelected();
}

void InputActionsWindow::DrawProperties() {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Properties");
  ImGui::Separator();
  ImGui::BeginChild("properties");
  if (selection_.action < 0 || selection_.action >= static_cast<int>(desc_.actions.size())) {
    ImGui::TextDisabled("Select an action or binding.");
    ImGui::EndChild();
    return;
  }
  auto& action = desc_.actions[selection_.action];
  switch (selection_.kind) {
    case Selection::Kind::kAction:
      DrawActionProperties(action);
      break;
    case Selection::Kind::kBinding: {
      if (selection_.index >= static_cast<int>(action.bindings.size())) break;
      ImGui::SeparatorText("Binding");
      if (DrawControlPath("path", action.bindings[selection_.index])) MarkChanged();
      break;
    }
    case Selection::Kind::kComposite: {
      if (selection_.index >= static_cast<int>(action.composites.size())) break;
      ImGui::SeparatorText("2D Vector Composite");
      auto& composite = action.composites[selection_.index];
      for (int part = 0; part < 4; part++) {
        ImGui::PushID(part);
        if (DrawPathList(kPartNames[part], Part(composite, part))) MarkChanged();
        ImGui::PopID();
      }
      break;
    }
    case Selection::Kind::kNone:
      ImGui::TextDisabled("Select an action or binding.");
      break;
  }
  ImGui::EndChild();
}

void InputActionsWindow::DrawActionProperties(InputActionDesc& action) {
  ImGui::SeparatorText("Action");
  ImGui::SetNextItemWidth(-90);
  if (ImGui::InputText("Name", &action.name)) MarkChanged();
  if (ImGui::IsItemActivated()) {
    Checkpoint();
    name_before_edit_ = action.name;
  }
  auto taken = [&] {
    return std::any_of(desc_.actions.begin(), desc_.actions.end(), [&](const auto& other) {
      return &other != &action && other.map == action.map && other.name == action.name;
    });
  };
  if (ImGui::IsItemDeactivated()) {
    // '/' separates the map from the action in "Map/Action".
    std::erase(action.name, '/');
    if (action.name.empty()) action.name = name_before_edit_;
    if (taken()) action.name = UniqueActionName(action.map, action.name);
  } else if (taken()) {
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Another action has this name");
  }

  static constexpr const char* kTypes[] = {"Button", "Value", "Pass Through"};
  int type = static_cast<int>(action.type);
  ImGui::SetNextItemWidth(-90);
  if (ImGui::Combo("Action Type", &type, kTypes, IM_ARRAYSIZE(kTypes))) {
    Checkpoint();
    action.type = static_cast<InputAction::Type>(type);
    if (action.type == InputAction::Type::kButton) action.vector2 = false;
    MarkChanged();
  }
  ImGui::SetItemTooltip(
      "Button: pressed / released, fires once per press.\n"
      "Value: a continuous value; started, performed while it changes, canceled at rest.\n"
      "Pass Through: performed on every change, from whichever control moved.");
  if (action.type != InputAction::Type::kButton) {
    static constexpr const char* kControls[] = {"Axis", "Vector 2"};
    int control = action.vector2 ? 1 : 0;
    ImGui::SetNextItemWidth(-90);
    if (ImGui::Combo("Control Type", &control, kControls, IM_ARRAYSIZE(kControls))) {
      Checkpoint();
      action.vector2 = control == 1;
      MarkChanged();
    }
  }

  // How a script reads it, through the generated header or by name.
  ImGui::SeparatorText("In scripts");
  const std::string member =
      desc_.header.empty()
          ? std::format("GetInputActions()->Find(\"{}\")", action.FullName())
          : std::format("input.{}.{}", InputActionIdentifier(action.map),
                        InputActionIdentifier(action.name));
  std::string usage;
  if (action.type == InputAction::Type::kButton) {
    usage = member + "->WasPressedThisFrame()";
  } else if (action.vector2) {
    usage = member + "->ReadValue<glm::vec2>()";
  } else {
    usage = member + "->ReadValue<float>()";
  }
  ImGui::TextDisabled("%s", usage.c_str());
  if (ImGui::IsItemClicked()) ImGui::SetClipboardText(usage.c_str());
  ImGui::SetItemTooltip("Click to copy");
  if (desc_.header.empty()) {
    ImGui::TextDisabled("Code > Generate C++ Header for typed access.");
  }
  for (const auto& error : header_errors_) {
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error.c_str());
  }
}

bool InputActionsWindow::DrawPathList(const char* label, std::vector<std::string>& paths) {
  bool changed = false;
  ImGui::SeparatorText(label);
  std::optional<int> remove;
  for (int i = 0; i < static_cast<int>(paths.size()); i++) {
    ImGui::PushID(i);
    changed |= DrawControlPath("path", paths[i]);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) remove = i;
    ImGui::SetItemTooltip("Remove");
    ImGui::PopID();
  }
  if (ImGui::SmallButton("+ Add")) {
    Checkpoint();
    paths.emplace_back();
    changed = true;
  }
  if (remove) {
    Checkpoint();
    paths.erase(paths.begin() + *remove);
    changed = true;
  }
  return changed;
}

bool InputActionsWindow::DrawControlPath(const char* id, std::string& path) {
  ImGui::PushID(id);
  const ImGuiID listen_id = ImGui::GetID("listen");
  bool changed = false;
  if (listen_id_ == listen_id && listened_) {
    Checkpoint();
    path = *listened_;
    listened_.reset();
    listen_id_ = 0;
    changed = true;
  }

  const float buttons = ImGui::CalcTextSize("Listen").x + ImGui::CalcTextSize("v").x +
                        ImGui::GetStyle().FramePadding.x * 4 +
                        ImGui::GetStyle().ItemSpacing.x * 2;
  ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - buttons - 30.0f));
  if (listen_id_ == listen_id) {
    std::string waiting = "Press a key or gamepad button (Esc cancels)";
    ImGui::BeginDisabled();
    ImGui::InputText("##path", &waiting, ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
  } else {
    if (ImGui::InputTextWithHint("##path", "<Keyboard>/space", &path)) changed = true;
    if (ImGui::IsItemActivated()) Checkpoint();
  }
  if (!path.empty() && path.front() == '<' && path.find('>') == std::string::npos) {
    ImGui::SetItemTooltip("Control paths look like <Device>/control");
  }

  ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
  if (ImGui::ArrowButton("##pick", ImGuiDir_Down)) {
    picker_filter_[0] = '\0';
    ImGui::OpenPopup("pick");
  }
  ImGui::SetItemTooltip("Pick a control");
  if (ImGui::BeginPopup("pick")) {
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##filter", "Search", picker_filter_, sizeof(picker_filter_));
    const std::string filter = picker_filter_;
    auto matches = [&](std::string_view candidate) {
      if (filter.empty()) return true;
      auto it = std::search(candidate.begin(), candidate.end(), filter.begin(), filter.end(),
                            [](char a, char b) { return std::tolower(a) == std::tolower(b); });
      return it != candidate.end();
    };
    for (const char* device : {"<Keyboard>/", "<Mouse>/", "<Gamepad>/"}) {
      const std::string_view prefix = device;
      bool header = false;
      for (std::string_view candidate : KnownControlPaths()) {
        if (!candidate.starts_with(prefix) || !matches(candidate)) continue;
        if (!header) {
          ImGui::SeparatorText(std::string(prefix.substr(1, prefix.size() - 3)).c_str());
          header = true;
        }
        const std::string text(candidate);
        if (ImGui::Selectable(text.c_str(), text == path)) {
          Checkpoint();
          path = text;
          changed = true;
        }
      }
    }
    ImGui::EndPopup();
  }

  ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
  const bool listening = listen_id_ == listen_id;
  if (listening) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.35f, 0.2f, 1.0f));
  if (ImGui::Button("Listen")) {
    if (listening) {
      listen_id_ = 0;
    } else {
      listen_id_ = listen_id;
      listened_.reset();
      std::fill(std::begin(listen_pad_), std::end(listen_pad_), true);
    }
  }
  if (listening) ImGui::PopStyleColor();
  ImGui::SetItemTooltip("Press the key or gamepad button to bind");
  ImGui::PopID();
  return changed;
}

}  // namespace Mercury
