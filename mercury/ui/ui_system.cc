#include "ui_system.h"

#include <algorithm>

#include "input/input_manager.h"

namespace Mercury::UI {

UISystem::UISystem() = default;

UISystem::~UISystem() { Clear(); }

Panel* UISystem::GetPanel(const std::string& settings_path,
                          const PanelSettings& settings) {
  for (const auto& entry : panels_) {
    if (entry.settings_path == settings_path) return entry.panel;
  }
  auto panel = std::make_unique<Panel>(&resources_);
  panel->SetSettings(settings);
  panel->clipboard.get = [] { return InputManager::GetClipboard(); };
  panel->clipboard.set = [](const std::string& text) { InputManager::SetClipboard(text); };
  Panel* raw = panel.get();
  panel_list_.push_back(std::move(panel));
  panels_.push_back({settings_path, raw});
  return raw;
}

VisualElement* UISystem::AddDocument(Panel* panel,
                                     const std::shared_ptr<VisualTreeAsset>& tree,
                                     float sort_order) {
  auto root = std::make_unique<TemplateContainer>();
  root->AddToClassList("unity-ui-document__root");
  root->SetPickingMode(PickingMode::kIgnore);
  root->SetPseudoState(kPseudoRoot, true);
  auto& s = root->style();
  s.position = Position::kAbsolute;
  s.left = s.top = s.right = s.bottom = Length::Px(0);
  if (tree) {
    root->templateId = tree->path;
    tree->CloneTree(root.get(), resources_);
  }
  // Keep documents in sort order: the last child draws on top.
  int index = 0;
  for (const auto& doc : documents_) {
    if (doc.panel == panel && doc.sort_order <= sort_order) index++;
  }
  VisualElement* raw = panel->visualTree()->Insert(index, std::move(root));
  documents_.push_back({raw, panel, sort_order});
  return raw;
}

void UISystem::RemoveDocument(VisualElement* root) {
  auto it = std::find_if(documents_.begin(), documents_.end(),
                         [root](const DocumentEntry& d) { return d.root == root; });
  if (it == documents_.end()) return;
  root->RemoveFromHierarchy();
  documents_.erase(it);
}

void UISystem::Clear() {
  documents_.clear();
  panels_.clear();
  panel_list_.clear();
  draw_data_.Clear();
  keyboard_panel_ = nullptr;
}

std::vector<Panel*> UISystem::PanelsTopFirst() const {
  std::vector<Panel*> panels;
  for (const auto& p : panel_list_) panels.push_back(p.get());
  std::stable_sort(panels.begin(), panels.end(), [](Panel* a, Panel* b) {
    return a->settings().sort_order > b->settings().sort_order;
  });
  return panels;
}

void UISystem::ProcessInput(const InputManager& input) {
  if (panel_list_.empty()) return;
  UIInputFrame frame;
  // The cursor is in window coordinates; panels work in framebuffer pixels.
  frame.pointer = const_cast<InputManager&>(input).GetCursorPos() * input.GetFramebufferScale();
  frame.pointer_inside = input.IsCursorInWindow();
  frame.mouse_available = !input.ImGuiWantsMouse();
  frame.keyboard_available = !input.ImGuiWantsKeyboard();

  UIInputFrame pointer_events = frame;
  UIInputFrame key_events = frame;
  for (const InputEvent& e : input.GetFrameEvents()) {
    UIInputEvent ui;
    ui.modifiers = e.mods;
    switch (e.type) {
      case InputEvent::Type::kMouseButton:
        if (e.action == GLFW_REPEAT) continue;
        ui.type = e.action == GLFW_PRESS ? UIInputEvent::Type::kPointerDown
                                         : UIInputEvent::Type::kPointerUp;
        ui.button = e.button;
        pointer_events.events.push_back(ui);
        break;
      case InputEvent::Type::kScroll:
        ui.type = UIInputEvent::Type::kWheel;
        // GLFW reports +y when scrolling up; the UI scrolls content down.
        ui.wheel = -e.scroll;
        pointer_events.events.push_back(ui);
        break;
      case InputEvent::Type::kKey:
        ui.type = e.action == GLFW_RELEASE ? UIInputEvent::Type::kKeyUp
                                           : UIInputEvent::Type::kKeyDown;
        ui.key = static_cast<KeyCode>(e.key);
        key_events.events.push_back(ui);
        break;
      case InputEvent::Type::kChar:
        ui.type = UIInputEvent::Type::kChar;
        ui.character = e.codepoint;
        key_events.events.push_back(ui);
        break;
    }
  }

  // The pointer belongs to the topmost panel with an element under it.
  const auto panels = PanelsTopFirst();
  Panel* pointer_panel = nullptr;
  for (Panel* panel : panels) {
    if (panel->Pick(frame.pointer / panel->scale())) {
      pointer_panel = panel;
      break;
    }
  }
  for (const auto& e : pointer_events.events) {
    // Clicking a panel gives it the keyboard.
    if (e.type == UIInputEvent::Type::kPointerDown && pointer_panel) {
      keyboard_panel_ = pointer_panel;
    }
  }
  if (!keyboard_panel_ || !keyboard_panel_->focusedElement()) {
    for (Panel* panel : panels) {
      if (panel->focusedElement()) {
        keyboard_panel_ = panel;
        break;
      }
    }
  }
  if (!keyboard_panel_) keyboard_panel_ = panels.front();

  for (Panel* panel : panels) {
    UIInputFrame panel_frame;
    panel_frame.pointer = frame.pointer;
    panel_frame.mouse_available = frame.mouse_available;
    panel_frame.keyboard_available = frame.keyboard_available;
    // Panels below the one under the pointer see it leave.
    panel_frame.pointer_inside =
        frame.pointer_inside && (pointer_panel == nullptr || panel == pointer_panel);
    if (panel == pointer_panel || (!pointer_panel && panel == panels.front())) {
      panel_frame.events = pointer_events.events;
    } else {
      // Releases still reach every panel so drags always end.
      for (const auto& e : pointer_events.events) {
        if (e.type == UIInputEvent::Type::kPointerUp) panel_frame.events.push_back(e);
      }
    }
    if (panel == keyboard_panel_) {
      panel_frame.events.insert(panel_frame.events.end(), key_events.events.begin(),
                                key_events.events.end());
    }
    panel->DispatchInput(panel_frame);
  }
}

void UISystem::Update(double dt_seconds, glm::vec2 screen_size, float dpi_scale) {
  draw_data_.Clear();
  if (panel_list_.empty()) return;
  auto panels = PanelsTopFirst();
  std::reverse(panels.begin(), panels.end());  // draw bottom first
  for (Panel* panel : panels) {
    panel->Update(dt_seconds, screen_size, dpi_scale);
    draw_data_.Append(panel->drawData());
  }
  draw_data_.target_size = glm::ivec2(screen_size);
}

bool UISystem::IsPointerOverUI() const {
  for (const auto& panel : panel_list_) {
    if (panel->IsPointerOverUI()) return true;
  }
  return false;
}

bool UISystem::WantsKeyboard() const {
  for (const auto& panel : panel_list_) {
    if (panel->textInputActive()) return true;
  }
  return false;
}

}  // namespace Mercury::UI
