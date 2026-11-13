#include "input_actions_window.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>

#include "input/input_actions_codegen.h"
#include "os/filesystem.h"
#include "templates.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxUndoSteps = 200;

std::vector<std::string>* PartVector(InputActionDesc::Composite2D& composite,
                                     int part) {
  switch (part) {
    case 0:
      return &composite.up;
    case 1:
      return &composite.down;
    case 2:
      return &composite.left;
    case 3:
      return &composite.right;
    default:
      return nullptr;
  }
}

// Writes `text` unless the file already holds it, so a save that changes
// nothing in the generated file does not rebuild every script that imports it.
bool WriteIfChanged(const fs::path& file, const std::string& text) {
  if (auto current = os::ReadText(file); current && *current == text)
    return true;
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
  edit_open_ = false;
  last_committed_ = {desc_, map_, selection_};
  header_errors_.clear();
  state_revision_++;
  return true;
}

void InputActionsWindow::Close() {
  path_.clear();
  desc_ = {};
  dirty_ = false;
  map_ = 0;
  selection_ = {};
  undo_.clear();
  redo_.clear();
  edit_open_ = false;
  last_committed_ = {};
  header_errors_.clear();
  state_revision_++;
}

bool InputActionsWindow::Save() {
  if (!project_ || path_.empty()) return false;
  CommitEdit();
  if (!SaveInputActions(project_->Resolve(path_), desc_)) {
    console_.Error("Input Actions: could not write " + path_);
    return false;
  }
  dirty_ = false;
  if (!desc_.header.empty() && !WriteHeader()) return false;
  console_.Info("Saved " + path_);
  state_revision_++;
  return true;
}

bool InputActionsWindow::WriteHeader() {
  if (!project_ || desc_.header.empty()) return false;
  const std::string class_name =
      desc_.class_name.empty() ? "GameInput" : desc_.class_name;
  auto header = GenerateInputActionsAngelScript(desc_, class_name, path_);
  header_errors_ = std::move(header.errors);
  state_revision_++;
  if (!header_errors_.empty()) {
    for (const auto& e : header_errors_) console_.Error("Input Actions: " + e);
    console_.Error("Input Actions: " + desc_.header +
                   " was not written; rename the actions");
    return false;
  }
  if (!WriteIfChanged(project_->Resolve(desc_.header), header.source)) {
    console_.Error("Input Actions: could not write " + desc_.header);
    return false;
  }
  return true;
}

bool InputActionsWindow::IsProjectActions() const {
  return project_ && !path_.empty() && project_->GetInputActionsPath() == path_;
}

void InputActionsWindow::SetAsProjectActions() {
  if (!project_ || path_.empty()) return;
  project_->SetInputActionsPath(path_);
  project_->Save();
  console_.Info(path_ + " is now the project's input actions");
  state_revision_++;
}

void InputActionsWindow::Checkpoint() {
  CommitEdit();
  undo_.push_back({desc_, map_, selection_});
  if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
  redo_.clear();
  last_committed_ = {desc_, map_, selection_};
}

void InputActionsWindow::BeginEdit() {
  if (!edit_open_) {
    last_committed_ = {desc_, map_, selection_};
    edit_open_ = true;
  }
}

void InputActionsWindow::CommitEdit() {
  if (!edit_open_) return;
  if (desc_ != last_committed_.desc) {
    undo_.push_back(std::move(last_committed_));
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    last_committed_ = {desc_, map_, selection_};
  }
  edit_open_ = false;
}

void InputActionsWindow::MarkChanged(bool coalesced) {
  if (coalesced && !edit_open_) BeginEdit();
  dirty_ = true;
  if (!coalesced) last_committed_ = {desc_, map_, selection_};
  state_revision_++;
}

void InputActionsWindow::Undo() {
  CommitEdit();
  if (undo_.empty()) return;
  redo_.push_back({desc_, map_, selection_});
  desc_ = std::move(undo_.back().desc);
  map_ = undo_.back().map;
  selection_ = undo_.back().selection;
  undo_.pop_back();
  last_committed_ = {desc_, map_, selection_};
  dirty_ = true;
  state_revision_++;
}

void InputActionsWindow::Redo() {
  CommitEdit();
  if (redo_.empty()) return;
  undo_.push_back({desc_, map_, selection_});
  desc_ = std::move(redo_.back().desc);
  map_ = redo_.back().map;
  selection_ = redo_.back().selection;
  redo_.pop_back();
  last_committed_ = {desc_, map_, selection_};
  dirty_ = true;
  state_revision_++;
}

const std::string& InputActionsWindow::SelectedMap() const {
  static const std::string kNone;
  if (map_ < 0 || map_ >= static_cast<int>(desc_.maps.size())) return kNone;
  return desc_.maps[map_];
}

void InputActionsWindow::SelectMap(int index) {
  if (index < 0 || index >= static_cast<int>(desc_.maps.size())) return;
  if (map_ != index) {
    map_ = index;
    selection_ = {};
    state_revision_++;
  }
}

void InputActionsWindow::Select(Selection selection) {
  if (selection_ != selection) {
    selection_ = selection;
    state_revision_++;
  }
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
    return std::any_of(
        desc_.actions.begin(), desc_.actions.end(),
        [&](const auto& a) { return a.map == map && a.name == name; });
  };
  std::string name = base;
  for (int i = 1; taken(name); i++) name = base + std::to_string(i);
  return name;
}

std::string InputActionsWindow::UniqueMapName(std::string base) const {
  std::string name = base;
  for (int i = 1; std::find(desc_.maps.begin(), desc_.maps.end(), name) !=
                  desc_.maps.end();
       i++) {
    name = base + std::to_string(i);
  }
  return name;
}

bool InputActionsWindow::IsActionNameTaken(int action_idx,
                                           const std::string& name) const {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return false;
  const auto& target = desc_.actions[action_idx];
  for (int i = 0; i < static_cast<int>(desc_.actions.size()); i++) {
    if (i != action_idx && desc_.actions[i].map == target.map &&
        desc_.actions[i].name == name) {
      return true;
    }
  }
  return false;
}

std::string InputActionsWindow::ActionUsageSnippet(int action_idx) const {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return "";
  const auto& action = desc_.actions[action_idx];
  const std::string class_name = desc_.class_name.empty()
                                     ? "GameInput"
                                     : InputActionIdentifier(desc_.class_name);
  const std::string member =
      desc_.header.empty()
          ? std::format("Input::FindAction(\"{}\")", action.FullName())
          : std::format("Input::RequireAction({}::{}.{})", class_name,
                        InputActionIdentifier(action.map),
                        InputActionIdentifier(action.name));
  if (action.type == InputAction::Type::kButton)
    return member + ".wasPressedThisFrame";
  if (action.vector2) return member + ".ReadVector2()";
  return member + ".ReadFloat()";
}

int InputActionsWindow::AddMap(std::string name) {
  Checkpoint();
  desc_.maps.push_back(name.empty() ? UniqueMapName("New action map")
                                    : UniqueMapName(std::move(name)));
  map_ = static_cast<int>(desc_.maps.size()) - 1;
  selection_ = {};
  MarkChanged();
  return map_;
}

bool InputActionsWindow::RenameMap(const std::string& from,
                                   const std::string& to) {
  if (to.empty() || to == from || to.find('/') != std::string::npos ||
      std::find(desc_.maps.begin(), desc_.maps.end(), to) != desc_.maps.end()) {
    return false;
  }
  Checkpoint();
  for (auto& map : desc_.maps) {
    if (map == from) map = to;
  }
  for (auto& action : desc_.actions) {
    if (action.map == from) action.map = to;
  }
  MarkChanged();
  return true;
}

void InputActionsWindow::DeleteMap(const std::string& map) {
  Checkpoint();
  std::erase(desc_.maps, map);
  std::erase_if(desc_.actions, [&](const auto& a) { return a.map == map; });
  map_ =
      std::clamp(map_, 0, std::max(0, static_cast<int>(desc_.maps.size()) - 1));
  selection_ = {};
  MarkChanged();
}

int InputActionsWindow::AddAction(const std::string& map) {
  if (desc_.maps.empty()) return -1;
  Checkpoint();
  InputActionDesc action;
  action.map = map;
  action.name = UniqueActionName(map, "New action");
  action.bindings.emplace_back();
  // Keep the map's actions together, so the file reads in window order.
  const auto actions = ActionsOf(map);
  const int index = actions.empty() ? static_cast<int>(desc_.actions.size())
                                    : actions.back() + 1;
  desc_.actions.insert(desc_.actions.begin() + index, std::move(action));
  selection_ = {Selection::Kind::kAction, index};
  MarkChanged();
  return index;
}

int InputActionsWindow::AddBinding(int action_idx) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return -1;
  Checkpoint();
  auto& action = desc_.actions[action_idx];
  action.bindings.emplace_back();
  const int binding_idx = static_cast<int>(action.bindings.size()) - 1;
  selection_ = {Selection::Kind::kBinding, action_idx, binding_idx};
  MarkChanged();
  return binding_idx;
}

int InputActionsWindow::AddComposite(int action_idx) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return -1;
  Checkpoint();
  auto& action = desc_.actions[action_idx];
  action.composites.emplace_back();
  if (action.type == InputAction::Type::kButton)
    action.type = InputAction::Type::kValue;
  action.vector2 = true;
  const int comp_idx = static_cast<int>(action.composites.size()) - 1;
  selection_ = {Selection::Kind::kComposite, action_idx, comp_idx};
  MarkChanged();
  return comp_idx;
}

void InputActionsWindow::DeleteSelected() {
  if (selection_.action < 0 ||
      selection_.action >= static_cast<int>(desc_.actions.size())) {
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
      if (selection_.index >= 0 &&
          selection_.index < static_cast<int>(action.bindings.size())) {
        action.bindings.erase(action.bindings.begin() + selection_.index);
      }
      selection_ = {Selection::Kind::kAction, selection_.action};
      break;
    case Selection::Kind::kComposite:
      if (selection_.index >= 0 &&
          selection_.index < static_cast<int>(action.composites.size())) {
        action.composites.erase(action.composites.begin() + selection_.index);
      }
      selection_ = {Selection::Kind::kAction, selection_.action};
      break;
    case Selection::Kind::kNone:
      return;
  }
  MarkChanged();
}

void InputActionsWindow::DuplicateSelected() {
  if (selection_.action < 0 ||
      selection_.action >= static_cast<int>(desc_.actions.size())) {
    return;
  }
  Checkpoint();
  auto& action = desc_.actions[selection_.action];
  switch (selection_.kind) {
    case Selection::Kind::kAction: {
      InputActionDesc copy = action;
      copy.name = UniqueActionName(copy.map, copy.name);
      desc_.actions.insert(desc_.actions.begin() + selection_.action + 1,
                           std::move(copy));
      selection_.action++;
      break;
    }
    case Selection::Kind::kBinding:
      if (selection_.index >= 0 &&
          selection_.index < static_cast<int>(action.bindings.size())) {
        action.bindings.insert(action.bindings.begin() + selection_.index + 1,
                               action.bindings[selection_.index]);
        selection_.index++;
      }
      break;
    case Selection::Kind::kComposite:
      if (selection_.index >= 0 &&
          selection_.index < static_cast<int>(action.composites.size())) {
        action.composites.insert(
            action.composites.begin() + selection_.index + 1,
            action.composites[selection_.index]);
        selection_.index++;
      }
      break;
    case Selection::Kind::kNone:
      return;
  }
  MarkChanged();
}

std::string InputActionsWindow::SetActionName(int action_idx,
                                              std::string new_name,
                                              bool finalize) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return "";
  auto& action = desc_.actions[action_idx];
  BeginEdit();
  if (finalize) {
    std::erase(new_name, '/');
    if (new_name.empty()) {
      new_name =
          last_committed_.desc.actions.size() > static_cast<size_t>(action_idx)
              ? last_committed_.desc.actions[action_idx].name
              : "Action";
    }
    if (IsActionNameTaken(action_idx, new_name)) {
      new_name = UniqueActionName(action.map, new_name);
    }
    action.name = new_name;
    dirty_ = true;
    CommitEdit();
    state_revision_++;
  } else {
    action.name = new_name;
    dirty_ = true;
    state_revision_++;
  }
  return action.name;
}

void InputActionsWindow::SetActionType(int action_idx, InputAction::Type type) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  Checkpoint();
  auto& action = desc_.actions[action_idx];
  action.type = type;
  if (action.type == InputAction::Type::kButton) action.vector2 = false;
  MarkChanged();
}

void InputActionsWindow::SetActionVector2(int action_idx, bool vector2) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  Checkpoint();
  desc_.actions[action_idx].vector2 = vector2;
  MarkChanged();
}

void InputActionsWindow::SetBindingPath(int action_idx, int binding_idx,
                                        const std::string& path,
                                        bool coalesced) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  auto& bindings = desc_.actions[action_idx].bindings;
  if (binding_idx < 0 || binding_idx >= static_cast<int>(bindings.size()))
    return;
  if (!coalesced) Checkpoint();
  bindings[binding_idx] = path;
  MarkChanged(coalesced);
}

void InputActionsWindow::SetCompositePartPath(int action_idx, int composite_idx,
                                              int part, int idx,
                                              const std::string& path,
                                              bool coalesced) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  auto& composites = desc_.actions[action_idx].composites;
  if (composite_idx < 0 || composite_idx >= static_cast<int>(composites.size()))
    return;
  auto* vec = PartVector(composites[composite_idx], part);
  if (!vec || idx < 0 || idx >= static_cast<int>(vec->size())) return;
  if (!coalesced) Checkpoint();
  (*vec)[idx] = path;
  MarkChanged(coalesced);
}

void InputActionsWindow::AddCompositePartPath(int action_idx, int composite_idx,
                                              int part) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  auto& composites = desc_.actions[action_idx].composites;
  if (composite_idx < 0 || composite_idx >= static_cast<int>(composites.size()))
    return;
  auto* vec = PartVector(composites[composite_idx], part);
  if (!vec) return;
  Checkpoint();
  vec->emplace_back();
  MarkChanged();
}

void InputActionsWindow::RemoveCompositePartPath(int action_idx,
                                                 int composite_idx, int part,
                                                 int idx) {
  if (action_idx < 0 || action_idx >= static_cast<int>(desc_.actions.size()))
    return;
  auto& composites = desc_.actions[action_idx].composites;
  if (composite_idx < 0 || composite_idx >= static_cast<int>(composites.size()))
    return;
  auto* vec = PartVector(composites[composite_idx], part);
  if (!vec || idx < 0 || idx >= static_cast<int>(vec->size())) return;
  Checkpoint();
  vec->erase(vec->begin() + idx);
  MarkChanged();
}

void InputActionsWindow::SetGenerateHeader(bool enabled) {
  Checkpoint();
  if (enabled) {
    if (desc_.class_name.empty()) desc_.class_name = "GameInput";
    desc_.header = "res://scripts/generated/" +
                   Templates::ToSnakeCase(desc_.class_name) + ".as";
  } else {
    desc_.header.clear();
  }
  MarkChanged();
}

void InputActionsWindow::SetClassName(const std::string& class_name,
                                      bool coalesced) {
  if (!coalesced) Checkpoint();
  desc_.class_name = class_name;
  MarkChanged(coalesced);
}

void InputActionsWindow::SetHeaderPath(const std::string& header,
                                       bool coalesced) {
  if (!coalesced) Checkpoint();
  desc_.header = header;
  MarkChanged(coalesced);
}

std::optional<std::string> InputActionsWindow::PollGamepadListen(
    const InputManager* input, bool prev_buttons[15]) {
  if (!input) return std::nullopt;
  const GamepadState& pad = input->GetGamepad();
  if (!pad.connected) return std::nullopt;
  for (int button = 0; button < 15; button++) {
    const bool pressed = pad.buttons[button];
    const bool was_pressed = prev_buttons[button];
    prev_buttons[button] = pressed;
    if (pressed && !was_pressed) {
      if (std::string control = ControlFromGamepadButton(button);
          !control.empty()) {
        return "<Gamepad>/" + control;
      }
    }
  }
  if (pad.axes[4] > 0.5f) return "<Gamepad>/leftTrigger";
  if (pad.axes[5] > 0.5f) return "<Gamepad>/rightTrigger";
  return std::nullopt;
}

}  // namespace Mercury
