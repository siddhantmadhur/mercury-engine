#include "ui_builder.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

#include "misc/cpp/imgui_stdlib.h"
#include "ui/controls.h"
#include "ui/style_resolver.h"

namespace Mercury {

namespace fs = std::filesystem;
using namespace UI;

namespace {

constexpr const char* kTypePayload = "MERCURY_UI_TYPE";
constexpr const char* kElementPayload = "MERCURY_UI_ELEMENT";
constexpr const char* kTemplatePayload = "MERCURY_UI_TEMPLATE";
constexpr const char* kAssetPayload = "MERCURY_ASSET";
constexpr size_t kMaxUndoSteps = 200;

const ImU32 kSelectionColor = IM_COL32(60, 150, 255, 255);
const ImU32 kHoverColor = IM_COL32(120, 190, 255, 140);
const ImU32 kMarginColor = IM_COL32(255, 160, 60, 60);
const ImU32 kPaddingColor = IM_COL32(110, 200, 110, 60);

bool ReadFile(const fs::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

bool WriteFile(const fs::path& path, const std::string& text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;
  file << text;
  return static_cast<bool>(file);
}

// Unity's Inspector groups for style properties.
struct StyleGroup {
  const char* name;
  std::vector<StylePropertyId> properties;
};

const std::vector<StyleGroup>& StyleGroups() {
  using P = StylePropertyId;
  static const std::vector<StyleGroup> kGroups = {
      {"Display", {P::kOpacity, P::kDisplay, P::kVisibility, P::kOverflow}},
      {"Position", {P::kPosition, P::kLeft, P::kTop, P::kRight, P::kBottom}},
      {"Flex",
       {P::kFlexBasis, P::kFlexShrink, P::kFlexGrow, P::kFlexDirection, P::kFlexWrap}},
      {"Align", {P::kAlignItems, P::kJustifyContent, P::kAlignSelf, P::kAlignContent}},
      {"Size",
       {P::kWidth, P::kHeight, P::kMinWidth, P::kMinHeight, P::kMaxWidth, P::kMaxHeight}},
      {"Margin & Padding",
       {P::kMarginLeft, P::kMarginTop, P::kMarginRight, P::kMarginBottom,
        P::kPaddingLeft, P::kPaddingTop, P::kPaddingRight, P::kPaddingBottom}},
      {"Text",
       {P::kFont, P::kFontStyle, P::kFontSize, P::kColor, P::kTextAlign, P::kWhiteSpace,
        P::kTextOverflow, P::kLetterSpacing, P::kWordSpacing, P::kParagraphSpacing,
        P::kTextOutlineWidth, P::kTextOutlineColor, P::kTextShadow}},
      {"Background",
       {P::kBackgroundColor, P::kBackgroundImage, P::kBackgroundImageTintColor,
        P::kBackgroundScaleMode, P::kSliceLeft, P::kSliceTop, P::kSliceRight,
        P::kSliceBottom, P::kSliceScale}},
      {"Border",
       {P::kBorderLeftColor, P::kBorderTopColor, P::kBorderRightColor,
        P::kBorderBottomColor, P::kBorderLeftWidth, P::kBorderTopWidth,
        P::kBorderRightWidth, P::kBorderBottomWidth, P::kBorderTopLeftRadius,
        P::kBorderTopRightRadius, P::kBorderBottomRightRadius,
        P::kBorderBottomLeftRadius}},
      {"Transform", {P::kTransformOrigin, P::kTranslate, P::kScale, P::kRotate}},
      {"Transition Animations",
       {P::kTransitionProperty, P::kTransitionDuration, P::kTransitionTimingFunction,
        P::kTransitionDelay}},
  };
  return kGroups;
}

// "Label #score .big" for the hierarchy.
std::string NodeLabel(const UxmlNode& node) {
  std::string label = node.type;
  if (node.type == "Instance") {
    if (auto t = node.FindAttribute("template")) label = "Instance of " + *t;
  }
  if (auto name = node.FindAttribute("name"); name && !name->empty()) {
    label += "  #" + *name;
  }
  if (auto cls = node.FindAttribute("class")) {
    std::istringstream in(*cls);
    std::string c;
    while (in >> c) label += " ." + c;
  }
  return label;
}

bool EditLength(const char* id, Length& length, bool allow_none) {
  bool changed = false;
  ImGui::PushID(id);
  const float unit_width = 58.0f;
  const bool numeric = length.unit == Length::Unit::kPixel ||
                       length.unit == Length::Unit::kPercent;
  ImGui::SetNextItemWidth(std::max(30.0f, ImGui::GetContentRegionAvail().x - unit_width - 4));
  ImGui::BeginDisabled(!numeric);
  float value = length.value;
  if (ImGui::DragFloat("##v", &value, 0.5f, 0.0f, 0.0f, "%.4g")) {
    length.value = value;
    changed = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine(0, 4);
  ImGui::SetNextItemWidth(unit_width);
  const char* units[] = {"px", "%", "auto", "none"};
  int unit = static_cast<int>(length.unit);
  if (ImGui::Combo("##unit", &unit, units, allow_none ? 4 : 3)) {
    length.unit = static_cast<Length::Unit>(unit);
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

bool EditColor(const char* id, Color& color) {
  return ImGui::ColorEdit4(id, &color.rgba.x,
                           ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
}

bool EditPath(const char* id, std::string& path) {
  bool changed = ImGui::InputTextWithHint(id, "res:// or relative path", &path,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
  if (ImGui::BeginDragDropTarget()) {
    if (auto payload = ImGui::AcceptDragDropPayload(kAssetPayload)) {
      path = static_cast<const char*>(payload->Data);
      changed = true;
    }
    ImGui::EndDragDropTarget();
  }
  return changed;
}

// Editor for one style value; true if it changed.
bool EditStyleValue(StylePropertyId id, StyleValue& value) {
  ImGui::SetNextItemWidth(-1);
  return std::visit(
      [&](auto& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Length>) {
          const bool none = id == StylePropertyId::kMaxWidth ||
                            id == StylePropertyId::kMaxHeight;
          return EditLength("##len", v, none);
        } else if constexpr (std::is_same_v<T, float>) {
          return ImGui::DragFloat("##f", &v, id == StylePropertyId::kOpacity ? 0.01f : 0.1f,
                                  0.0f, 0.0f, "%.4g");
        } else if constexpr (std::is_same_v<T, Color>) {
          return EditColor("##c", v);
        } else if constexpr (std::is_same_v<T, int>) {
          const auto& keywords = StyleKeywords(id);
          const char* current =
              v >= 0 && v < static_cast<int>(keywords.size()) ? keywords[v].c_str() : "";
          bool changed = false;
          if (ImGui::BeginCombo("##k", current)) {
            for (size_t i = 0; i < keywords.size(); i++) {
              if (ImGui::Selectable(keywords[i].c_str(), static_cast<int>(i) == v)) {
                v = static_cast<int>(i);
                changed = true;
              }
            }
            ImGui::EndCombo();
          }
          return changed;
        } else if constexpr (std::is_same_v<T, Background>) {
          return EditPath("##bg", v.path);
        } else if constexpr (std::is_same_v<T, FontDefinition>) {
          return EditPath("##font", v.path);
        } else if constexpr (std::is_same_v<T, Translate>) {
          bool a = EditLength("##x", v.x, false);
          bool b = EditLength("##y", v.y, false);
          return a || b;
        } else if constexpr (std::is_same_v<T, TransformOrigin>) {
          bool a = EditLength("##x", v.x, false);
          bool b = EditLength("##y", v.y, false);
          return a || b;
        } else if constexpr (std::is_same_v<T, Scale>) {
          return ImGui::DragFloat2("##s", &v.value.x, 0.01f, 0.0f, 0.0f, "%.3g");
        } else if constexpr (std::is_same_v<T, Rotate>) {
          return ImGui::DragFloat("##r", &v.degrees, 0.5f, 0.0f, 0.0f, "%.3g deg");
        } else if constexpr (std::is_same_v<T, TextShadow>) {
          bool changed = ImGui::DragFloat2("##o", &v.offset.x, 0.1f, 0.0f, 0.0f, "%.3g");
          ImGui::SetNextItemWidth(-1);
          changed |= ImGui::DragFloat("##b", &v.blur_radius, 0.1f, 0.0f, 50.0f, "blur %.3g");
          ImGui::SetNextItemWidth(-1);
          changed |= EditColor("##c", v.color);
          return changed;
        } else {
          // Lists: edited as text by the caller.
          return false;
        }
      },
      value);
}

bool IsListValue(const StyleValue& v) {
  return std::holds_alternative<PropertyNameList>(v) || std::holds_alternative<TimeList>(v) ||
         std::holds_alternative<EasingList>(v);
}

}  // namespace

UIBuilder::UIBuilder(Console& console) : console_(console) {}

UIBuilder::~UIBuilder() = default;

void UIBuilder::SetProject(std::shared_ptr<Project> project) {
  Close();
  project_ = std::move(project);
}

// --------------------------------------------------------------- Document ---

bool UIBuilder::Open(const std::string& res_path) {
  if (!project_) return false;
  std::string text;
  if (!ReadFile(project_->Resolve(res_path), text)) {
    console_.Error("UI Builder: could not read " + res_path);
    return false;
  }
  std::vector<UxmlError> errors;
  auto doc = VisualTreeAsset::Parse(text, res_path, &errors);
  for (const auto& e : errors) {
    console_.Error(std::format("{}:{}: {}", res_path, e.line, e.message));
  }
  doc_ = doc;
  doc_path_ = res_path;
  dirty_ = false;
  undo_.clear();
  redo_.clear();
  selection_ = {Selection::Kind::kDocument};
  active_sheet_ = 0;
  LoadSheets();
  last_committed_ = TakeSnapshot();
  edit_open_ = false;
  preview_dirty_ = true;
  fit_on_open_ = true;
  return true;
}

void UIBuilder::Close() {
  doc_.reset();
  doc_path_.clear();
  sheets_.clear();
  panel_.reset();
  preview_root_ = nullptr;
  resources_.reset();
  undo_.clear();
  redo_.clear();
  dirty_ = false;
  selection_ = {};
}

void UIBuilder::LoadSheets() {
  sheets_.clear();
  if (!doc_) return;
  for (const auto& src : doc_->root.style_sheets) {
    SheetEntry entry;
    entry.src = src;
    entry.res_path = ResolveRelativePath(doc_path_, src);
    std::string text;
    if (!project_ || !ReadFile(project_->Resolve(entry.res_path), text)) {
      console_.Warn("UI Builder: could not read " + entry.res_path);
    }
    std::vector<StyleParseError> errors;
    entry.sheet = StyleSheet::Parse(text, entry.res_path, &errors);
    for (const auto& e : errors) {
      console_.Warn(std::format("{}:{}: {}", entry.res_path, e.line, e.message));
    }
    sheets_.push_back(std::move(entry));
  }
}

int UIBuilder::AddSheet(const std::string& src) {
  for (size_t i = 0; i < sheets_.size(); i++) {
    if (sheets_[i].src == src) return static_cast<int>(i);
  }
  doc_->root.style_sheets.push_back(src);
  SheetEntry entry;
  entry.src = src;
  entry.res_path = ResolveRelativePath(doc_path_, src);
  std::string text;
  if (project_ && ReadFile(project_->Resolve(entry.res_path), text)) {
    entry.sheet = StyleSheet::Parse(text, entry.res_path);
  } else {
    entry.sheet = std::make_shared<StyleSheet>();
    entry.sheet->path = entry.res_path;
    entry.dirty = true;  // new file, written on save
  }
  sheets_.push_back(std::move(entry));
  MarkChanged();
  return static_cast<int>(sheets_.size()) - 1;
}

std::string UIBuilder::RelativeToDocument(const std::string& res_path) const {
  if (!res_path.starts_with("res://") || !doc_path_.starts_with("res://")) return res_path;
  const fs::path target(res_path.substr(6));
  const fs::path base = fs::path(doc_path_.substr(6)).parent_path();
  return target.lexically_relative(base).generic_string();
}

UIBuilder::Snapshot UIBuilder::TakeSnapshot() const {
  Snapshot s;
  if (!doc_) return s;
  s.uxml = doc_->Write();
  for (const auto& sheet : sheets_) s.sheets.emplace_back(sheet.src, sheet.sheet->Write());
  s.selection = selection_;
  return s;
}

void UIBuilder::RestoreSnapshot(const Snapshot& snapshot) {
  doc_ = VisualTreeAsset::Parse(snapshot.uxml, doc_path_);
  std::vector<SheetEntry> sheets;
  for (const auto& [src, text] : snapshot.sheets) {
    SheetEntry entry;
    entry.src = src;
    entry.res_path = ResolveRelativePath(doc_path_, src);
    // Unchanged sheets stay as they are (and are not rewritten on save).
    auto same = std::find_if(sheets_.begin(), sheets_.end(), [&](const SheetEntry& old) {
      return old.src == src && old.sheet->Write() == text;
    });
    if (same != sheets_.end()) {
      entry.sheet = same->sheet;
      entry.dirty = same->dirty;
    } else {
      entry.sheet = StyleSheet::Parse(text, entry.res_path);
      entry.dirty = true;
    }
    sheets.push_back(std::move(entry));
  }
  sheets_ = std::move(sheets);
  selection_ = snapshot.selection;
  if (selection_.kind == Selection::Kind::kElement && !NodeAt(selection_.element)) {
    selection_ = {Selection::Kind::kDocument};
  }
  if (selection_.kind == Selection::Kind::kSelector && !SelectedRule()) {
    selection_ = {Selection::Kind::kDocument};
  }
  active_sheet_ = std::clamp(active_sheet_, 0, std::max(0, int(sheets_.size()) - 1));
  dirty_ = true;
  preview_dirty_ = true;
}

void UIBuilder::MarkChanged() {
  if (!edit_open_) {
    undo_.push_back(last_committed_);
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    edit_open_ = true;
  }
  dirty_ = true;
  preview_dirty_ = true;
}

void UIBuilder::EndFrameEdits() {
  if (edit_open_ && !ImGui::IsAnyItemActive()) {
    last_committed_ = TakeSnapshot();
    edit_open_ = false;
  }
}

void UIBuilder::Undo() {
  if (undo_.empty() || !doc_) return;
  if (edit_open_) {
    last_committed_ = TakeSnapshot();
    edit_open_ = false;
  }
  redo_.push_back(last_committed_);
  last_committed_ = undo_.back();
  undo_.pop_back();
  RestoreSnapshot(last_committed_);
}

void UIBuilder::Redo() {
  if (redo_.empty() || !doc_) return;
  undo_.push_back(last_committed_);
  last_committed_ = redo_.back();
  redo_.pop_back();
  RestoreSnapshot(last_committed_);
}

bool UIBuilder::Save() {
  if (!doc_ || !project_) return false;
  bool ok = WriteFile(project_->Resolve(doc_path_), doc_->Write());
  if (!ok) console_.Error("UI Builder: could not write " + doc_path_);
  for (auto& sheet : sheets_) {
    // Untouched sheets keep their formatting and comments.
    if (!sheet.dirty) continue;
    if (WriteFile(project_->Resolve(sheet.res_path), sheet.sheet->Write())) {
      sheet.dirty = false;
    } else {
      console_.Error("UI Builder: could not write " + sheet.res_path);
      ok = false;
    }
  }
  if (ok) {
    dirty_ = false;
    console_.Info("Saved " + doc_path_);
  }
  return ok;
}

UxmlNode* UIBuilder::NodeAt(const std::vector<int>& path) {
  if (!doc_) return nullptr;
  UxmlNode* node = &doc_->root;
  for (int index : path) {
    if (index < 0 || index >= static_cast<int>(node->children.size())) return nullptr;
    node = &node->children[index];
  }
  return node;
}

std::optional<std::vector<int>> UIBuilder::PathOf(const UxmlNode* target) const {
  if (!doc_ || !target) return std::nullopt;
  std::vector<int> path;
  std::function<bool(const UxmlNode&)> find = [&](const UxmlNode& node) {
    if (&node == target) return true;
    for (size_t i = 0; i < node.children.size(); i++) {
      path.push_back(static_cast<int>(i));
      if (find(node.children[i])) return true;
      path.pop_back();
    }
    return false;
  };
  if (!find(doc_->root)) return std::nullopt;
  return path;
}

UxmlNode* UIBuilder::SelectedNode() {
  if (selection_.kind != Selection::Kind::kElement) return nullptr;
  return NodeAt(selection_.element);
}

StyleRule* UIBuilder::SelectedRule() {
  if (selection_.kind != Selection::Kind::kSelector) return nullptr;
  if (selection_.sheet < 0 || selection_.sheet >= static_cast<int>(sheets_.size())) {
    return nullptr;
  }
  auto& rules = sheets_[selection_.sheet].sheet->rules;
  if (selection_.rule < 0 || selection_.rule >= static_cast<int>(rules.size())) return nullptr;
  return &rules[selection_.rule];
}

VisualElement* UIBuilder::ElementFor(const UxmlNode* node) const {
  if (!preview_root_ || !node) return nullptr;
  VisualElement* found = nullptr;
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    if (found) return;
    if (e->uxmlSource == node) {
      found = e;
      return;
    }
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(preview_root_);
  return found;
}

UxmlNode UIBuilder::NewNode(const std::string& type) const {
  UxmlNode node;
  node.type = type;
  // Like Unity, new controls show what they are.
  if (type == "Label" || type == "Button" || type == "RepeatButton") {
    node.attributes.push_back({"text", type});
  } else if (type == "Toggle" || type == "Slider" || type == "SliderInt" ||
             type == "TextField" || type == "IntegerField" || type == "FloatField" ||
             type == "RadioButton") {
    node.attributes.push_back({"label", type});
  } else if (type == "Foldout" || type == "GroupBox") {
    node.attributes.push_back({"text", type});
  } else if (type == "ProgressBar") {
    node.attributes.push_back({"title", "Progress"});
    node.attributes.push_back({"value", "50"});
  }
  return node;
}

void UIBuilder::InsertNode(const std::vector<int>& parent_path, int index, UxmlNode node) {
  UxmlNode* parent = NodeAt(parent_path);
  if (!parent) return;
  index = std::clamp(index, 0, static_cast<int>(parent->children.size()));
  parent->children.insert(parent->children.begin() + index, std::move(node));
  selection_.kind = Selection::Kind::kElement;
  selection_.element = parent_path;
  selection_.element.push_back(index);
  MarkChanged();
}

std::optional<UxmlNode> UIBuilder::RemoveNode(const std::vector<int>& path) {
  if (path.empty()) return std::nullopt;
  std::vector<int> parent_path(path.begin(), path.end() - 1);
  UxmlNode* parent = NodeAt(parent_path);
  if (!parent || path.back() >= static_cast<int>(parent->children.size())) {
    return std::nullopt;
  }
  UxmlNode node = std::move(parent->children[path.back()]);
  parent->children.erase(parent->children.begin() + path.back());
  MarkChanged();
  return node;
}

void UIBuilder::MoveNode(const std::vector<int>& from, const std::vector<int>& parent_path,
                         int index) {
  // Can't move an element into itself.
  if (parent_path.size() >= from.size() &&
      std::equal(from.begin(), from.end(), parent_path.begin())) {
    return;
  }
  std::vector<int> target = parent_path;
  // Removing shifts later siblings (and the target, if it comes after).
  if (from.size() <= target.size()) {
    const size_t depth = from.size() - 1;
    if (std::equal(from.begin(), from.end() - 1, target.begin()) &&
        target[depth] > from.back()) {
      target[depth]--;
    }
  }
  std::vector<int> from_parent(from.begin(), from.end() - 1);
  if (from_parent == parent_path && index > from.back()) index--;
  auto node = RemoveNode(from);
  if (!node) return;
  InsertNode(target, index, std::move(*node));
}

void UIBuilder::DeleteSelected() {
  if (selection_.kind == Selection::Kind::kElement) {
    RemoveNode(selection_.element);
    selection_ = {Selection::Kind::kDocument};
  } else if (selection_.kind == Selection::Kind::kSelector && SelectedRule()) {
    auto& rules = sheets_[selection_.sheet].sheet->rules;
    rules.erase(rules.begin() + selection_.rule);
    sheets_[selection_.sheet].dirty = true;
    selection_ = {Selection::Kind::kDocument};
    MarkChanged();
  }
}

void UIBuilder::DuplicateSelected() {
  UxmlNode* node = SelectedNode();
  if (!node) return;
  UxmlNode copy = *node;
  if (auto name = copy.FindAttribute("name"); name && !name->empty()) {
    copy.SetAttribute("name", *name + "-copy");
  }
  std::vector<int> parent(selection_.element.begin(), selection_.element.end() - 1);
  InsertNode(parent, selection_.element.back() + 1, std::move(copy));
}

// ---------------------------------------------------------------- Preview ---

void UIBuilder::RebuildPreview() {
  preview_dirty_ = false;
  preview_root_ = nullptr;
  if (!doc_) return;
  if (!resources_) {
    resources_ = std::make_unique<UIResources>();
    resources_->on_error = [this](const std::string& message) {
      console_.Warn("UI Builder: " + message);
    };
  }
  resources_->SetResolver([project = project_](std::string_view path) {
    return project ? project->Resolve(path) : fs::path(std::string(path));
  });
  resources_->ClearOverrides();
  // Templates and textures may have changed on disk.
  resources_->Reload();
  for (const auto& sheet : sheets_) resources_->Override(sheet.res_path, sheet.sheet);

  // A fresh panel each time keeps the preview exactly in step with the
  // document; documents are small.
  panel_ = std::make_unique<Panel>(resources_.get());
  PanelSettings settings;
  settings.scale = zoom_;
  settings.theme_style_sheet = theme_;
  panel_->SetSettings(settings);
  panel_->clipboard.get = [] {
    const char* text = ImGui::GetClipboardText();
    return std::string(text ? text : "");
  };
  panel_->clipboard.set = [](const std::string& text) { ImGui::SetClipboardText(text.c_str()); };
  auto root = std::make_unique<TemplateContainer>();
  root->AddToClassList("unity-ui-document__root");
  root->SetPickingMode(PickingMode::kIgnore);
  root->SetPseudoState(kPseudoRoot, true);
  auto& s = root->style();
  s.position = Position::kAbsolute;
  s.left = s.top = s.right = s.bottom = Length::Px(0);
  doc_->CloneTree(root.get(), *resources_);
  preview_root_ = panel_->visualTree()->Add(std::move(root));
}

void UIBuilder::UpdatePreview(RenderHost* host) {
  if (preview_dirty_ || !panel_) RebuildPreview();
  if (!panel_) return;
  PanelSettings settings = panel_->settings();
  if (settings.scale != zoom_ || settings.theme_style_sheet != theme_) {
    settings.scale = zoom_;
    settings.theme_style_sheet = theme_;
    panel_->SetSettings(settings);
  }
  const glm::vec2 pixels = glm::max(glm::vec2(canvas_size_) * zoom_, glm::vec2(1.0f));
  panel_->Update(ImGui::GetIO().DeltaTime, glm::round(pixels));
  if (host) {
    host->SetUIPreview(&panel_->drawData(), static_cast<int>(std::round(pixels.x)),
                       static_cast<int>(std::round(pixels.y)), background_);
  }
}

VisualElement* UIBuilder::PickForEditing(glm::vec2 point) const {
  if (!preview_root_) return nullptr;
  // Like picking, but ignores picking-mode and skips the inner parts of
  // controls: only elements from the document can be selected.
  VisualElement* found = nullptr;
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    if (e->resolvedStyle().display == DisplayStyle::kNone) return;
    if (e->uxmlSource && e != preview_root_ && e->worldBound().Contains(point)) found = e;
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(preview_root_);
  return found;
}

void UIBuilder::ForwardInput(ImVec2 origin) {
  ImGuiIO& io = ImGui::GetIO();
  UIInputFrame frame;
  frame.pointer = glm::vec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
  frame.pointer_inside = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  KeyModifiers mods = (io.KeyShift ? kModShift : 0) | (io.KeyCtrl ? kModControl : 0) |
                      (io.KeyAlt ? kModAlt : 0) | (io.KeySuper ? kModSuper : 0);
  for (int b = 0; b < 3; b++) {
    if (ImGui::IsMouseClicked(b) && frame.pointer_inside) {
      frame.events.push_back({UIInputEvent::Type::kPointerDown, b, KeyCode::kNone, 0, {}, mods});
    }
    if (ImGui::IsMouseReleased(b)) {
      frame.events.push_back({UIInputEvent::Type::kPointerUp, b, KeyCode::kNone, 0, {}, mods});
    }
  }
  if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
    UIInputEvent wheel;
    wheel.type = UIInputEvent::Type::kWheel;
    wheel.wheel = glm::vec2(-io.MouseWheelH, -io.MouseWheel);
    frame.events.push_back(wheel);
  }
  if (preview_keyboard_) {
    static const std::pair<ImGuiKey, KeyCode> kKeys[] = {
        {ImGuiKey_Enter, KeyCode::kEnter},       {ImGuiKey_KeypadEnter, KeyCode::kKeypadEnter},
        {ImGuiKey_Escape, KeyCode::kEscape},     {ImGuiKey_Tab, KeyCode::kTab},
        {ImGuiKey_Backspace, KeyCode::kBackspace}, {ImGuiKey_Delete, KeyCode::kDelete},
        {ImGuiKey_LeftArrow, KeyCode::kLeft},    {ImGuiKey_RightArrow, KeyCode::kRight},
        {ImGuiKey_UpArrow, KeyCode::kUp},        {ImGuiKey_DownArrow, KeyCode::kDown},
        {ImGuiKey_Home, KeyCode::kHome},         {ImGuiKey_End, KeyCode::kEnd},
        {ImGuiKey_A, KeyCode::kA},               {ImGuiKey_C, KeyCode::kC},
        {ImGuiKey_V, KeyCode::kV},               {ImGuiKey_X, KeyCode::kX},
        {ImGuiKey_Space, KeyCode::kSpace},
    };
    for (const auto& [imgui_key, key] : kKeys) {
      if (ImGui::IsKeyPressed(imgui_key, true)) {
        frame.events.push_back({UIInputEvent::Type::kKeyDown, 0, key, 0, {}, mods});
      }
      if (ImGui::IsKeyReleased(imgui_key)) {
        frame.events.push_back({UIInputEvent::Type::kKeyUp, 0, key, 0, {}, mods});
      }
    }
    for (ImWchar c : io.InputQueueCharacters) {
      frame.events.push_back({UIInputEvent::Type::kChar, 0, KeyCode::kNone,
                              static_cast<char32_t>(c), {}, mods});
    }
  }
  panel_->DispatchInput(frame);
}

// ------------------------------------------------------------------- Draw ---

void UIBuilder::Draw(RenderHost* host, bool* open) {
  ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(kWindowName, open, ImGuiWindowFlags_MenuBar)) {
    has_focus_ = false;
    ImGui::End();
    return;
  }
  has_focus_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  DrawToolbar();

  if (!doc_) {
    ImGui::Spacing();
    ImGui::TextDisabled("Open a .uxml file: double-click it in the Project panel,");
    ImGui::TextDisabled("or create one with Project > + Create > UI Document.");
    ImGui::End();
    return;
  }

  // Three resizable columns: StyleSheets/Hierarchy/Library | Viewport/Code |
  // Inspector, as in Unity.
  const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
  if (ImGui::BeginTable("ui_builder_columns", 3, flags)) {
    ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthFixed, left_width_);
    ImGui::TableSetupColumn("center", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, right_width_);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    const float left_height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("stylesheets", ImVec2(0, left_height * 0.28f), ImGuiChildFlags_ResizeY);
    DrawStyleSheets();
    ImGui::EndChild();
    ImGui::BeginChild("hierarchy", ImVec2(0, left_height * 0.40f), ImGuiChildFlags_ResizeY);
    DrawHierarchy();
    ImGui::EndChild();
    ImGui::BeginChild("library");
    DrawLibrary();
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(1);
    const float center_height = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("viewport", ImVec2(0, std::max(100.0f, center_height - code_height_)),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawViewport(host);
    ImGui::EndChild();
    ImGui::BeginChild("code");
    DrawCode();
    ImGui::EndChild();

    ImGui::TableSetColumnIndex(2);
    ImGui::BeginChild("inspector");
    DrawInspector();
    ImGui::EndChild();
    ImGui::EndTable();
  }

  if (has_focus_ && !ImGui::GetIO().WantTextInput && !preview_mode_) {
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) DeleteSelected();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelected();
  }
  EndFrameEdits();
  // After every edit this frame, so the draw data handed to the renderer
  // stays valid until it renders.
  UpdatePreview(host);
  ImGui::End();
}

void UIBuilder::DrawToolbar() {
  if (!ImGui::BeginMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Save", "Ctrl+S", false, doc_ != nullptr)) Save();
    if (ImGui::MenuItem("Revert", nullptr, false, doc_ && dirty_)) Open(doc_path_);
    if (ImGui::MenuItem("Close", nullptr, false, doc_ != nullptr)) Close();
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, CanUndo())) Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, CanRedo())) Redo();
    ImGui::Separator();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, SelectedNode() != nullptr)) {
      DuplicateSelected();
    }
    if (ImGui::MenuItem("Delete", "Del", false,
                        SelectedNode() != nullptr || SelectedRule() != nullptr)) {
      DeleteSelected();
    }
    ImGui::EndMenu();
  }
  if (doc_) {
    ImGui::Separator();
    ImGui::TextUnformatted(fs::path(doc_path_).filename().string().c_str());
    if (dirty_) ImGui::TextDisabled("*");
    ImGui::Separator();
    ImGui::Checkbox("Preview", &preview_mode_);
    ImGui::SetItemTooltip("Interact with the UI as the game would (hover, click, type)");
    ImGui::Separator();
    ImGui::SetNextItemWidth(120);
    float percent = zoom_ * 100.0f;
    if (ImGui::SliderFloat("##zoom", &percent, 10.0f, 400.0f, "Zoom %.0f%%",
                           ImGuiSliderFlags_Logarithmic)) {
      zoom_ = percent / 100.0f;
    }
    if (ImGui::Button("Fit")) fit_on_open_ = true;
    if (ImGui::Button("1:1")) {
      zoom_ = 1.0f;
      pan_ = {0, 0};
    }
  }
  ImGui::EndMenuBar();
}

// ------------------------------------------------------------ StyleSheets ---

void UIBuilder::DrawStyleSheets() {
  ImGui::SeparatorText("StyleSheets");
  // Add a selector to the active sheet, as in Unity's "Add new selector".
  ImGui::SetNextItemWidth(-60);
  const bool enter = ImGui::InputTextWithHint("##selector", ".class, #name, Type",
                                              new_selector_, sizeof(new_selector_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  std::string error;
  const std::string selector_text = new_selector_;
  bool valid = !selector_text.empty();
  for (size_t start = 0; valid && start <= selector_text.size();) {
    size_t comma = selector_text.find(',', start);
    if (comma == std::string::npos) comma = selector_text.size();
    valid = ParseSelector(selector_text.substr(start, comma - start), &error).has_value();
    start = comma + 1;
  }
  ImGui::BeginDisabled(!valid);
  if (ImGui::Button("Add") || (enter && valid)) {
    if (sheets_.empty()) {
      // Make a sheet next to the document.
      const std::string stem = fs::path(doc_path_).stem().string();
      active_sheet_ = AddSheet(stem + ".uss");
    }
    auto& sheet = sheets_[active_sheet_];
    sheet.sheet->AddRule(selector_text);
    sheet.dirty = true;
    selection_ = {Selection::Kind::kSelector, {}, active_sheet_,
                  static_cast<int>(sheet.sheet->rules.size()) - 1};
    new_selector_[0] = '\0';
    MarkChanged();
  }
  ImGui::EndDisabled();
  if (!selector_text.empty() && !valid) {
    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error.c_str());
  }

  int remove_sheet = -1;
  for (size_t s = 0; s < sheets_.size(); s++) {
    auto& entry = sheets_[s];
    ImGui::PushID(static_cast<int>(s));
    const bool active = static_cast<int>(s) == active_sheet_;
    const std::string label = entry.src + (entry.dirty ? " *" : "") + (active ? "  (active)" : "");
    const bool open = ImGui::TreeNodeEx("##sheet", ImGuiTreeNodeFlags_DefaultOpen |
                                                       ImGuiTreeNodeFlags_SpanAvailWidth,
                                        "%s", label.c_str());
    if (ImGui::IsItemClicked()) active_sheet_ = static_cast<int>(s);
    if (ImGui::BeginPopupContextItem()) {
      if (ImGui::MenuItem("Set as Active")) active_sheet_ = static_cast<int>(s);
      if (ImGui::MenuItem("Remove from Document")) remove_sheet = static_cast<int>(s);
      ImGui::EndPopup();
    }
    if (open) {
      int remove_rule = -1, duplicate_rule = -1;
      for (size_t r = 0; r < entry.sheet->rules.size(); r++) {
        ImGui::PushID(static_cast<int>(r));
        const bool selected = selection_.kind == Selection::Kind::kSelector &&
                              selection_.sheet == static_cast<int>(s) &&
                              selection_.rule == static_cast<int>(r);
        const std::string text = entry.sheet->rules[r].SelectorText();
        if (ImGui::Selectable(text.c_str(), selected)) {
          selection_ = {Selection::Kind::kSelector, {}, static_cast<int>(s), static_cast<int>(r)};
          active_sheet_ = static_cast<int>(s);
        }
        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem("Duplicate")) duplicate_rule = static_cast<int>(r);
          if (ImGui::MenuItem("Delete")) remove_rule = static_cast<int>(r);
          ImGui::EndPopup();
        }
        ImGui::PopID();
      }
      if (duplicate_rule >= 0) {
        StyleRule copy = entry.sheet->rules[duplicate_rule];
        entry.sheet->rules.insert(entry.sheet->rules.begin() + duplicate_rule + 1, copy);
        entry.dirty = true;
        MarkChanged();
      }
      if (remove_rule >= 0) {
        entry.sheet->rules.erase(entry.sheet->rules.begin() + remove_rule);
        entry.dirty = true;
        selection_ = {Selection::Kind::kDocument};
        MarkChanged();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (remove_sheet >= 0) {
    auto& list = doc_->root.style_sheets;
    std::erase(list, sheets_[remove_sheet].src);
    sheets_.erase(sheets_.begin() + remove_sheet);
    active_sheet_ = 0;
    selection_ = {Selection::Kind::kDocument};
    MarkChanged();
  }

  if (ImGui::Button("+ Style Sheet")) ImGui::OpenPopup("add_sheet");
  if (ImGui::BeginPopup("add_sheet")) {
    if (ImGui::MenuItem("New .uss next to the document")) {
      const std::string stem = fs::path(doc_path_).stem().string();
      std::string name = stem + ".uss";
      for (int i = 2; project_ && fs::exists(project_->Resolve(ResolveRelativePath(doc_path_, name))); i++) {
        name = stem + std::to_string(i) + ".uss";
      }
      active_sheet_ = AddSheet(name);
    }
    ImGui::SeparatorText("Existing");
    if (list_files) {
      for (const auto& file : list_files({".uss"})) {
        if (ImGui::MenuItem(file.c_str())) active_sheet_ = AddSheet(RelativeToDocument(file));
      }
    }
    ImGui::EndPopup();
  }
}

// -------------------------------------------------------------- Hierarchy ---

void UIBuilder::DrawHierarchy() {
  ImGui::SeparatorText("Hierarchy");
  const std::string doc_label = fs::path(doc_path_).filename().string();
  const bool doc_selected = selection_.kind == Selection::Kind::kDocument;
  if (ImGui::Selectable(doc_label.c_str(), doc_selected)) {
    selection_ = {Selection::Kind::kDocument};
  }
  // Dropping on the document adds at the top level.
  if (ImGui::BeginDragDropTarget()) {
    if (auto payload = ImGui::AcceptDragDropPayload(kTypePayload)) {
      InsertNode({}, static_cast<int>(doc_->root.children.size()),
                 NewNode(static_cast<const char*>(payload->Data)));
    }
    if (auto payload = ImGui::AcceptDragDropPayload(kElementPayload)) {
      const auto* data = static_cast<const int*>(payload->Data);
      std::vector<int> from(data, data + payload->DataSize / sizeof(int));
      MoveNode(from, {}, static_cast<int>(doc_->root.children.size()));
    }
    ImGui::EndDragDropTarget();
  }
  ImGui::Indent(8.0f);
  std::vector<int> path;
  for (size_t i = 0; i < doc_->root.children.size(); i++) {
    path.push_back(static_cast<int>(i));
    DrawHierarchyNode(doc_->root.children[i], path);
    path.pop_back();
  }
  ImGui::Unindent(8.0f);
  // Structural edits wait until the tree is drawn: they move nodes around.
  if (pending_edit_) {
    auto edit = std::move(pending_edit_);
    pending_edit_ = nullptr;
    edit();
  }
}

void UIBuilder::DrawHierarchyNode(UxmlNode& node, std::vector<int>& path) {
  ImGui::PushID(&node);
  const bool selected = selection_.kind == Selection::Kind::kElement && selection_.element == path;
  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                             ImGuiTreeNodeFlags_SpanAvailWidth;
  if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
  if (selected) flags |= ImGuiTreeNodeFlags_Selected;
  const bool open = ImGui::TreeNodeEx("##node", flags, "%s", NodeLabel(node).c_str());
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
    selection_ = {Selection::Kind::kElement, path};
  }
  if (ImGui::IsItemHovered()) hovered_node_ = &node;

  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(kElementPayload, path.data(), path.size() * sizeof(int));
    ImGui::TextUnformatted(NodeLabel(node).c_str());
    ImGui::EndDragDropSource();
  }
  const std::vector<int> here = path;
  const std::vector<int> parent(path.begin(), path.end() - 1);
  const int index = path.back();
  const int child_count = static_cast<int>(node.children.size());
  if (ImGui::BeginDragDropTarget()) {
    // Drop on an element: Shift puts it before, otherwise inside.
    const bool before = ImGui::GetIO().KeyShift;
    const std::vector<int> target_parent = before ? parent : here;
    const int target_index = before ? index : child_count;
    if (auto payload = ImGui::AcceptDragDropPayload(kTypePayload)) {
      const std::string type = static_cast<const char*>(payload->Data);
      pending_edit_ = [this, target_parent, target_index, type] {
        InsertNode(target_parent, target_index, NewNode(type));
      };
    } else if (auto payload = ImGui::AcceptDragDropPayload(kElementPayload)) {
      const auto* data = static_cast<const int*>(payload->Data);
      std::vector<int> from(data, data + payload->DataSize / sizeof(int));
      if (from != here) {
        pending_edit_ = [this, from, target_parent, target_index] {
          MoveNode(from, target_parent, target_index);
        };
      }
    }
    ImGui::EndDragDropTarget();
  }
  if (ImGui::BeginPopupContextItem()) {
    selection_ = {Selection::Kind::kElement, here};
    if (ImGui::BeginMenu("Add Child")) {
      for (const auto* type : UxmlFactoryRegistry::Instance().Types()) {
        if (type->name == "TemplateContainer") continue;
        if (ImGui::MenuItem(type->name.c_str())) {
          const std::string name = type->name;
          pending_edit_ = [this, here, child_count, name] {
            InsertNode(here, child_count, NewNode(name));
          };
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
      pending_edit_ = [this] { DuplicateSelected(); };
    }
    if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) {
      pending_edit_ = [this, here, parent, index] { MoveNode(here, parent, index - 1); };
    }
    if (ImGui::MenuItem("Move Down")) {
      pending_edit_ = [this, here, parent, index] { MoveNode(here, parent, index + 2); };
    }
    if (ImGui::MenuItem("Wrap in VisualElement")) {
      pending_edit_ = [this, here, parent, index] {
        if (auto removed = RemoveNode(here)) {
          UxmlNode wrapper = NewNode("VisualElement");
          wrapper.children.push_back(std::move(*removed));
          InsertNode(parent, index, std::move(wrapper));
        }
      };
    }
    if (ImGui::MenuItem("Unwrap", nullptr, false, child_count > 0)) {
      pending_edit_ = [this, here, parent, index] {
        if (auto removed = RemoveNode(here)) {
          for (size_t i = 0; i < removed->children.size(); i++) {
            InsertNode(parent, index + static_cast<int>(i), removed->children[i]);
          }
        }
      };
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete", "Del")) pending_edit_ = [this] { DeleteSelected(); };
    ImGui::EndPopup();
  }
  if (open) {
    for (size_t i = 0; i < node.children.size(); i++) {
      path.push_back(static_cast<int>(i));
      DrawHierarchyNode(node.children[i], path);
      path.pop_back();
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

// ---------------------------------------------------------------- Library ---

void UIBuilder::DrawLibrary() {
  ImGui::SeparatorText("Library");
  if (ImGui::BeginTabBar("library_tabs")) {
    if (ImGui::BeginTabItem("Standard")) {
      std::string category;
      for (const auto* type : UxmlFactoryRegistry::Instance().Types()) {
        if (type->name == "TemplateContainer" || type->name == "TextElement" ||
            type->name == "Scroller") {
          continue;
        }
        if (type->category != category) {
          category = type->category;
          ImGui::TextDisabled("%s", category.c_str());
        }
        ImGui::PushID(type->name.c_str());
        ImGui::Selectable(("   " + type->name).c_str(), false,
                          ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::BeginDragDropSource()) {
          ImGui::SetDragDropPayload(kTypePayload, type->name.c_str(), type->name.size() + 1);
          ImGui::Text("+ %s", type->name.c_str());
          ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          // Double-click adds to the selected element (or the document).
          std::vector<int> parent =
              selection_.kind == Selection::Kind::kElement ? selection_.element : std::vector<int>{};
          UxmlNode* p = NodeAt(parent);
          InsertNode(parent, p ? static_cast<int>(p->children.size()) : 0, NewNode(type->name));
        }
        ImGui::SetItemTooltip("Drag into the Hierarchy or the Viewport, or double-click");
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Project")) {
      ImGui::TextDisabled("UI documents, added as templates");
      if (list_files) {
        for (const auto& file : list_files({".uxml"})) {
          if (file == doc_path_) continue;
          ImGui::PushID(file.c_str());
          ImGui::Selectable(fs::path(file).filename().string().c_str());
          ImGui::SetItemTooltip("%s", file.c_str());
          if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(kTemplatePayload, file.c_str(), file.size() + 1);
            ImGui::Text("+ %s", file.c_str());
            ImGui::EndDragDropSource();
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
}

// --------------------------------------------------------------- Viewport ---

void UIBuilder::DrawViewport(RenderHost* host) {
  // Canvas settings strip.
  ImGui::SetNextItemWidth(70);
  ImGui::DragInt("##w", &canvas_size_.x, 1.0f, 16, 8192, "W %d");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70);
  ImGui::DragInt("##h", &canvas_size_.y, 1.0f, 16, 8192, "H %d");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  if (ImGui::BeginCombo("##preset", "Size", ImGuiComboFlags_NoArrowButton)) {
    const std::pair<const char*, glm::ivec2> presets[] = {
        {"1920 x 1080", {1920, 1080}}, {"1280 x 720", {1280, 720}},
        {"2560 x 1440", {2560, 1440}}, {"1024 x 768", {1024, 768}},
        {"800 x 600", {800, 600}},     {"720 x 1280 (portrait)", {720, 1280}}};
    for (const auto& [label, size] : presets) {
      if (ImGui::Selectable(label)) {
        canvas_size_ = size;
        fit_on_open_ = true;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::ColorEdit4("##bg", &background_.x, ImGuiColorEditFlags_NoInputs);
  ImGui::SetItemTooltip("Canvas background");
  ImGui::SameLine();
  ImGui::TextDisabled(preview_mode_ ? "Preview: interact with the UI"
                                    : "Click to select, drag handles to resize, "
                                      "middle-drag or Alt-drag to pan, Ctrl+wheel to zoom");

  const ImVec2 area_min = ImGui::GetCursorScreenPos();
  const ImVec2 area = ImGui::GetContentRegionAvail();
  viewport_size_ = area;
  if (area.x < 8 || area.y < 8) return;
  if (fit_on_open_) {
    zoom_ = std::min((area.x - 40) / canvas_size_.x, (area.y - 40) / canvas_size_.y);
    zoom_ = std::clamp(zoom_, 0.1f, 4.0f);
    pan_ = {0, 0};
    fit_on_open_ = false;
  }

  ImGui::InvisibleButton("canvas", area,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(area_min, ImVec2(area_min.x + area.x, area_min.y + area.y), true);
  draw->AddRectFilled(area_min, ImVec2(area_min.x + area.x, area_min.y + area.y),
                      IM_COL32(28, 28, 30, 255));

  const ImVec2 canvas_px(std::round(canvas_size_.x * zoom_), std::round(canvas_size_.y * zoom_));
  const ImVec2 origin(std::round(area_min.x + (area.x - canvas_px.x) * 0.5f + pan_.x),
                      std::round(area_min.y + (area.y - canvas_px.y) * 0.5f + pan_.y));
  const ImVec2 corner(origin.x + canvas_px.x, origin.y + canvas_px.y);
  const uint64_t texture = host ? host->GetUIPreviewTexture() : 0;
  if (texture) {
    draw->AddImage(static_cast<ImTextureID>(texture), origin, corner);
  } else {
    draw->AddRectFilled(origin, corner,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(background_.r, background_.g,
                                                              background_.b, background_.a)));
    draw->AddText(ImVec2(origin.x + 8, origin.y + 8), IM_COL32(200, 200, 200, 255),
                  "Starting renderer...");
  }
  draw->AddRect(ImVec2(origin.x - 1, origin.y - 1), ImVec2(corner.x + 1, corner.y + 1),
                IM_COL32(90, 90, 95, 255));

  if (preview_mode_ && panel_) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) preview_keyboard_ = hovered;
    ForwardInput(origin);
  } else {
    preview_keyboard_ = false;
    HandleCanvasMouse(origin, hovered, active);
    DrawCanvasOverlay(draw, origin);
  }

  // Library items and templates can be dropped onto the canvas.
  if (ImGui::BeginDragDropTarget()) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    VisualElement* target =
        PickForEditing(glm::vec2((mouse.x - origin.x) / zoom_, (mouse.y - origin.y) / zoom_));
    std::vector<int> parent;
    if (target) {
      if (auto p = PathOf(static_cast<const UxmlNode*>(target->uxmlSource))) parent = *p;
    }
    UxmlNode* parent_node = NodeAt(parent);
    const int index = parent_node ? static_cast<int>(parent_node->children.size()) : 0;
    if (auto payload = ImGui::AcceptDragDropPayload(kTypePayload)) {
      InsertNode(parent, index, NewNode(static_cast<const char*>(payload->Data)));
    } else if (auto payload = ImGui::AcceptDragDropPayload(kTemplatePayload)) {
      const std::string file = static_cast<const char*>(payload->Data);
      std::string name = fs::path(file).stem().string();
      if (!doc_->FindTemplate(name)) {
        doc_->templates.push_back({name, RelativeToDocument(file)});
      }
      UxmlNode instance;
      instance.type = "Instance";
      instance.attributes.push_back({"template", name});
      InsertNode(parent, index, std::move(instance));
    }
    ImGui::EndDragDropTarget();
  }
  draw->PopClipRect();
}

void UIBuilder::HandleCanvasMouse(ImVec2 origin, bool hovered, bool active) {
  ImGuiIO& io = ImGui::GetIO();
  const glm::vec2 point((io.MousePos.x - origin.x) / zoom_, (io.MousePos.y - origin.y) / zoom_);

  // Zoom around the mouse with Ctrl+wheel.
  if (hovered && io.KeyCtrl && io.MouseWheel != 0.0f) {
    const float old_zoom = zoom_;
    zoom_ = std::clamp(zoom_ * std::pow(1.15f, io.MouseWheel), 0.1f, 4.0f);
    const float ratio = zoom_ / old_zoom;
    pan_.x *= ratio;
    pan_.y *= ratio;
  } else if (hovered && io.MouseWheel != 0.0f) {
    pan_.y += io.MouseWheel * 40.0f;
    pan_.x += io.MouseWheelH * 40.0f;
  }

  UxmlNode* node = SelectedNode();
  VisualElement* element = ElementFor(node);
  VisualElement* hover = hovered ? PickForEditing(point) : nullptr;
  hovered_node_ = hover ? static_cast<const UxmlNode*>(hover->uxmlSource) : hovered_node_;

  // Handle positions of the selection (screen space).
  auto handle_at = [&](ImVec2 mouse) -> Handle {
    if (!element) return Handle::kNone;
    const Rect b = element->worldBound();
    const ImVec2 min(origin.x + b.x * zoom_, origin.y + b.y * zoom_);
    const ImVec2 max(origin.x + b.xMax() * zoom_, origin.y + b.yMax() * zoom_);
    auto near = [&](ImVec2 p) {
      return std::abs(mouse.x - p.x) <= 6 && std::abs(mouse.y - p.y) <= 6;
    };
    if (near(max)) return Handle::kCorner;
    if (near(ImVec2(max.x, (min.y + max.y) / 2))) return Handle::kRight;
    if (near(ImVec2((min.x + max.x) / 2, max.y))) return Handle::kBottom;
    if (mouse.x >= min.x && mouse.y >= min.y && mouse.x <= max.x && mouse.y <= max.y &&
        element->resolvedStyle().position == Position::kAbsolute) {
      return Handle::kMove;
    }
    return Handle::kNone;
  };

  if (ImGui::IsItemActivated()) {
    drag_start_mouse_ = io.MousePos;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || io.KeyAlt) {
      drag_ = Handle::kPan;
    } else {
      drag_ = handle_at(io.MousePos);
      if (drag_ == Handle::kNone || (drag_ == Handle::kMove && hover &&
                                     hover->uxmlSource != node)) {
        // Plain click: select what is under the mouse.
        if (hover) {
          if (auto path = PathOf(static_cast<const UxmlNode*>(hover->uxmlSource))) {
            selection_ = {Selection::Kind::kElement, *path};
          }
        } else {
          selection_ = {Selection::Kind::kDocument};
        }
        drag_ = Handle::kNone;
        element = ElementFor(SelectedNode());
        if (element && element->resolvedStyle().position == Position::kAbsolute) {
          drag_ = Handle::kMove;
        }
      }
      if (element) {
        drag_start_size_ = {element->layout().width, element->layout().height};
        drag_start_pos_ = {element->layout().x, element->layout().y};
      }
    }
  }
  if (!active) {
    drag_ = Handle::kNone;
    // Cursor feedback.
    switch (handle_at(io.MousePos)) {
      case Handle::kRight: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
      case Handle::kBottom: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
      case Handle::kCorner: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
      default: break;
    }
    return;
  }

  const ImVec2 delta(io.MousePos.x - drag_start_mouse_.x, io.MousePos.y - drag_start_mouse_.y);
  if (drag_ == Handle::kPan) {
    pan_.x += io.MouseDelta.x;
    pan_.y += io.MouseDelta.y;
    return;
  }
  node = SelectedNode();
  if (!node || drag_ == Handle::kNone || (delta.x == 0 && delta.y == 0)) return;
  auto px = [](float v) { return std::format("{}px", static_cast<int>(std::round(v))); };
  switch (drag_) {
    case Handle::kRight:
      SetStyle(node, nullptr, "width", px(drag_start_size_.x + delta.x / zoom_));
      break;
    case Handle::kBottom:
      SetStyle(node, nullptr, "height", px(drag_start_size_.y + delta.y / zoom_));
      break;
    case Handle::kCorner:
      SetStyle(node, nullptr, "width", px(drag_start_size_.x + delta.x / zoom_));
      SetStyle(node, nullptr, "height", px(drag_start_size_.y + delta.y / zoom_));
      break;
    case Handle::kMove:
      if (std::abs(delta.x) + std::abs(delta.y) < 3) return;  // a click, not a drag
      SetStyle(node, nullptr, "left", px(drag_start_pos_.x + delta.x / zoom_));
      SetStyle(node, nullptr, "top", px(drag_start_pos_.y + delta.y / zoom_));
      break;
    default: break;
  }
}

void UIBuilder::DrawCanvasOverlay(ImDrawList* draw, ImVec2 origin) {
  auto to_screen = [&](float x, float y) {
    return ImVec2(origin.x + x * zoom_, origin.y + y * zoom_);
  };
  if (hovered_node_ && hovered_node_ != SelectedNode()) {
    if (auto* hover = ElementFor(hovered_node_)) {
      const Rect b = hover->worldBound();
      draw->AddRect(to_screen(b.x, b.y), to_screen(b.xMax(), b.yMax()), kHoverColor, 0, 0, 1.0f);
    }
  }
  hovered_node_ = nullptr;

  VisualElement* element = ElementFor(SelectedNode());
  if (!element) return;
  const Rect b = element->worldBound();
  const ComputedStyle& s = element->resolvedStyle();
  const float parent_width = element->hierarchyParent() ? element->hierarchyParent()->layout().width : 0;
  // Margin (outside) and padding (inside) bands, as in Unity's viewport.
  const float ml = s.marginLeft.Resolve(parent_width), mr = s.marginRight.Resolve(parent_width);
  const float mt = s.marginTop.Resolve(parent_width), mb = s.marginBottom.Resolve(parent_width);
  const ImVec2 outer_min = to_screen(b.x - ml, b.y - mt), outer_max = to_screen(b.xMax() + mr, b.yMax() + mb);
  const ImVec2 min = to_screen(b.x, b.y), max = to_screen(b.xMax(), b.yMax());
  draw->AddRectFilled(outer_min, ImVec2(outer_max.x, min.y), kMarginColor);
  draw->AddRectFilled(ImVec2(outer_min.x, max.y), outer_max, kMarginColor);
  draw->AddRectFilled(ImVec2(outer_min.x, min.y), ImVec2(min.x, max.y), kMarginColor);
  draw->AddRectFilled(ImVec2(max.x, min.y), ImVec2(outer_max.x, max.y), kMarginColor);
  const Rect content = element->contentRect();
  const ImVec2 cmin = to_screen(b.x + content.x, b.y + content.y);
  const ImVec2 cmax = to_screen(b.x + content.xMax(), b.y + content.yMax());
  draw->AddRectFilled(min, ImVec2(max.x, cmin.y), kPaddingColor);
  draw->AddRectFilled(ImVec2(min.x, cmax.y), max, kPaddingColor);
  draw->AddRectFilled(ImVec2(min.x, cmin.y), ImVec2(cmin.x, cmax.y), kPaddingColor);
  draw->AddRectFilled(ImVec2(cmax.x, cmin.y), ImVec2(max.x, cmax.y), kPaddingColor);

  draw->AddRect(min, max, kSelectionColor, 0, 0, 2.0f);
  for (ImVec2 h : {max, ImVec2(max.x, (min.y + max.y) / 2), ImVec2((min.x + max.x) / 2, max.y)}) {
    draw->AddRectFilled(ImVec2(h.x - 4, h.y - 4), ImVec2(h.x + 4, h.y + 4), IM_COL32_WHITE);
    draw->AddRect(ImVec2(h.x - 4, h.y - 4), ImVec2(h.x + 4, h.y + 4), kSelectionColor);
  }
  const std::string size = std::format("{} x {}", static_cast<int>(b.width), static_cast<int>(b.height));
  draw->AddText(ImVec2(min.x, max.y + 4), kSelectionColor, size.c_str());
}

// --------------------------------------------------------------- Inspector ---

void UIBuilder::DrawInspector() {
  ImGui::SeparatorText("Inspector");
  if (selection_.kind == Selection::Kind::kDocument || selection_.kind == Selection::Kind::kNone) {
    DrawDocumentSettings();
    return;
  }
  if (StyleRule* rule = SelectedRule()) {
    std::string text = rule->SelectorText();
    ImGui::TextDisabled("Selector");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##selector_text", &text, ImGuiInputTextFlags_EnterReturnsTrue)) {
      StyleRule renamed;
      bool ok = true;
      for (size_t start = 0; start <= text.size();) {
        size_t comma = text.find(',', start);
        if (comma == std::string::npos) comma = text.size();
        auto parsed = ParseSelector(text.substr(start, comma - start));
        if (!parsed) ok = false;
        else renamed.selectors.push_back(*parsed);
        start = comma + 1;
      }
      if (ok && !renamed.selectors.empty()) {
        rule->selectors = std::move(renamed.selectors);
        sheets_[selection_.sheet].dirty = true;
        MarkChanged();
      } else {
        console_.Error("\"" + text + "\" is not a valid selector");
      }
    }
    ImGui::TextDisabled("in %s", sheets_[selection_.sheet].src.c_str());
    ImGui::Spacing();
    DrawStyleProperties(nullptr, rule);
    return;
  }
  UxmlNode* node = SelectedNode();
  if (!node) return;
  ImGui::Text("%s", node->type.c_str());
  if (const auto* type = UxmlFactoryRegistry::Instance().Find(node->type)) {
    (void)type;
  }
  ImGui::Spacing();
  if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawAttributes(*node);
  }
  if (!SelectedNode()) return;
  if (ImGui::CollapsingHeader("Style Class List", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawClassList(*node);
  }
  if (ImGui::CollapsingHeader("Bindings")) DrawBindings(*node);
  if (ImGui::CollapsingHeader("Matching Selectors")) {
    DrawMatchingSelectors(ElementFor(node));
  }
  ImGui::SeparatorText("Inline Styles");
  DrawStyleProperties(node, nullptr);
}

void UIBuilder::DrawDocumentSettings() {
  ImGui::TextUnformatted(fs::path(doc_path_).filename().string().c_str());
  ImGui::TextDisabled("%s", doc_path_.c_str());
  ImGui::Spacing();
  ImGui::SeparatorText("Canvas");
  ImGui::DragInt2("Size", &canvas_size_.x, 1.0f, 16, 8192);
  ImGui::ColorEdit4("Background", &background_.x);
  std::string theme = theme_;
  ImGui::SetNextItemWidth(-90);
  if (ImGui::BeginCombo("Theme", theme.empty() ? "Default runtime theme" : theme.c_str())) {
    if (ImGui::Selectable("Default runtime theme", theme_.empty())) theme_.clear();
    if (list_files) {
      for (const auto& file : list_files({".uss"})) {
        if (ImGui::Selectable(file.c_str(), file == theme_)) theme_ = file;
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SetItemTooltip("Preview only; the game uses its PanelSettings' theme");
  ImGui::SeparatorText("Templates");
  int remove = -1;
  for (size_t i = 0; i < doc_->templates.size(); i++) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::BulletText("%s  =  %s", doc_->templates[i].name.c_str(), doc_->templates[i].src.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
    ImGui::PopID();
  }
  if (remove >= 0) {
    doc_->templates.erase(doc_->templates.begin() + remove);
    MarkChanged();
  }
  if (doc_->templates.empty()) ImGui::TextDisabled("Drag .uxml files from Library > Project");
}

void UIBuilder::DrawAttributes(UxmlNode& node) {
  const UxmlTypeInfo* type = UxmlFactoryRegistry::Instance().Find(node.type);
  std::vector<UxmlAttributeDescription> attributes;
  if (node.type == "Instance") {
    attributes = VisualElementAttributes();
    UxmlAttributeDescription tmpl{"template", FieldType::kEnum, ""};
    for (const auto& t : doc_->templates) tmpl.enum_values.push_back(t.name);
    attributes.insert(attributes.begin(), tmpl);
  } else if (type) {
    attributes = type->attributes;
  } else {
    ImGui::TextDisabled("Unknown type; attributes are kept as they are");
    for (auto& a : node.attributes) attributes.push_back({a.name, FieldType::kString, ""});
  }
  for (const auto& desc : attributes) {
    if (desc.name == "class" || desc.name == "style") continue;
    ImGui::PushID(desc.name.c_str());
    const std::string* stored = node.FindAttribute(desc.name);
    std::string value = stored ? *stored : desc.default_value;
    ImGui::AlignTextToFramePadding();
    if (stored) {
      ImGui::TextUnformatted(desc.name.c_str());
    } else {
      ImGui::TextDisabled("%s", desc.name.c_str());
    }
    if (!desc.tooltip.empty()) ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::SetNextItemWidth(-1);
    bool changed = false;
    switch (desc.type) {
      case FieldType::kBool: {
        bool b = value == "true" || value == "True" || value == "1";
        if (ImGui::Checkbox("##v", &b)) {
          value = b ? "true" : "false";
          changed = true;
        }
        break;
      }
      case FieldType::kInt: {
        int i = std::atoi(value.c_str());
        if (ImGui::DragInt("##v", &i)) {
          value = std::to_string(i);
          changed = true;
        }
        break;
      }
      case FieldType::kFloat: {
        float f = std::strtof(value.c_str(), nullptr);
        if (ImGui::DragFloat("##v", &f, 0.1f, 0.0f, 0.0f, "%.4g")) {
          value = std::format("{}", f);
          changed = true;
        }
        break;
      }
      case FieldType::kEnum: {
        if (ImGui::BeginCombo("##v", value.c_str())) {
          for (const auto& option : desc.enum_values) {
            if (ImGui::Selectable(option.c_str(), option == value)) {
              value = option;
              changed = true;
            }
          }
          ImGui::EndCombo();
        }
        break;
      }
      case FieldType::kColor: {
        Color c = ParseColor(value).value_or(Color::White());
        if (EditColor("##v", c)) {
          value = FormatStyleValue(StylePropertyId::kColor, c);
          changed = true;
        }
        break;
      }
      case FieldType::kAsset: changed = EditPath("##v", value); break;
      default: {
        // Multi-line text (e.g. a Label) is edited with \n escapes.
        changed = ImGui::InputText("##v", &value);
        break;
      }
    }
    if (ImGui::BeginPopupContextItem("attr_menu")) {
      if (ImGui::MenuItem("Unset", nullptr, false, stored != nullptr)) {
        node.RemoveAttribute(desc.name);
        MarkChanged();
      }
      ImGui::EndPopup();
    }
    if (changed) {
      if (value == desc.default_value && desc.name != "text") {
        node.RemoveAttribute(desc.name);
      } else {
        node.SetAttribute(desc.name, value);
      }
      MarkChanged();
    }
    ImGui::PopID();
  }
}

void UIBuilder::DrawClassList(UxmlNode& node) {
  std::vector<std::string> classes;
  if (auto cls = node.FindAttribute("class")) {
    std::istringstream in(*cls);
    std::string c;
    while (in >> c) classes.push_back(c);
  }
  auto write = [&] {
    std::string joined;
    for (const auto& c : classes) joined += (joined.empty() ? "" : " ") + c;
    if (joined.empty()) {
      node.RemoveAttribute("class");
    } else {
      node.SetAttribute("class", joined);
    }
    MarkChanged();
  };
  ImGui::SetNextItemWidth(-50);
  const bool enter = ImGui::InputTextWithHint("##class", "class-name", new_class_,
                                              sizeof(new_class_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  std::string name = new_class_;
  if (name.starts_with('.')) name = name.substr(1);
  if ((ImGui::Button("Add") || enter) && !name.empty() &&
      std::find(classes.begin(), classes.end(), name) == classes.end()) {
    classes.push_back(name);
    new_class_[0] = '\0';
    write();
    return;
  }
  int remove = -1;
  for (size_t i = 0; i < classes.size(); i++) {
    ImGui::PushID(static_cast<int>(i));
    if (i > 0) ImGui::SameLine();
    if (ImGui::SmallButton(("." + classes[i] + "  x").c_str())) remove = static_cast<int>(i);
    ImGui::SetItemTooltip("Remove class");
    ImGui::PopID();
  }
  if (remove >= 0) {
    classes.erase(classes.begin() + remove);
    write();
    return;
  }
  if (!InlineDeclarations(node).empty()) {
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-140);
    ImGui::InputTextWithHint("##extract", "new-class", extract_class_, sizeof(extract_class_));
    ImGui::SameLine();
    std::string extract = extract_class_;
    if (extract.starts_with('.')) extract = extract.substr(1);
    ImGui::BeginDisabled(extract.empty());
    if (ImGui::Button("Extract inline styles")) {
      ExtractInlineStylesToClass(node, extract);
      extract_class_[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Move this element's inline styles into a new .class selector");
  }
}

void UIBuilder::DrawBindings(UxmlNode& node) {
  ImGui::TextDisabled("Data source path -> element property, updated every frame");
  const UxmlTypeInfo* type = UxmlFactoryRegistry::Instance().Find(node.type);
  int remove = -1;
  for (size_t i = 0; i < node.bindings.size(); i++) {
    auto& b = node.bindings[i];
    ImGui::PushID(static_cast<int>(i));
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.32f);
    if (ImGui::BeginCombo("##prop", b.property.c_str())) {
      if (type) {
        for (const auto& a : type->attributes) {
          if (a.name == "class" || a.name == "style") continue;
          if (ImGui::Selectable(a.name.c_str(), a.name == b.property)) {
            b.property = a.name;
            changed = true;
          }
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    changed |= ImGui::InputTextWithHint("##path", "data-source-path", &b.data_source_path);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 24);
    const char* modes[] = {"TwoWay", "ToSource", "ToTarget", "ToTargetOnce"};
    int mode = static_cast<int>(b.mode);
    if (ImGui::Combo("##mode", &mode, modes, 4)) {
      b.mode = static_cast<BindingMode>(mode);
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
    if (changed) MarkChanged();
    ImGui::PopID();
  }
  if (remove >= 0) {
    node.bindings.erase(node.bindings.begin() + remove);
    MarkChanged();
  }
  if (ImGui::Button("+ Add Binding")) {
    node.bindings.push_back({node.FindAttribute("value") || (type && type->name != "Label" &&
                                                             type->name != "Button")
                                 ? "value"
                                 : "text",
                             "", BindingMode::kToTarget});
    MarkChanged();
  }
}

void UIBuilder::DrawMatchingSelectors(VisualElement* element) {
  if (!element || !resources_ || !panel_) {
    ImGui::TextDisabled("(not in the preview)");
    return;
  }
  std::vector<const StyleSheet*> panel_sheets;
  if (auto theme = theme_.empty() ? resources_->DefaultTheme() : resources_->LoadStyleSheet(theme_)) {
    resources_->CollectSheets(theme, panel_sheets);
  }
  const auto sheets = ApplicableSheets(element, *resources_, panel_sheets);
  const auto matches = MatchRules(element, sheets);
  if (matches.empty()) ImGui::TextDisabled("None");
  for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
    ImGui::PushID(&*it);
    const std::string file = fs::path(it->sheet->path).filename().string();
    // Selecting jumps to the rule if it is in one of the document's sheets.
    int sheet_index = -1;
    for (size_t s = 0; s < sheets_.size(); s++) {
      if (sheets_[s].sheet.get() == it->sheet) sheet_index = static_cast<int>(s);
    }
    if (ImGui::Selectable(it->selector->text.c_str(), false, ImGuiSelectableFlags_None)) {
      if (sheet_index >= 0) {
        selection_ = {Selection::Kind::kSelector, {}, sheet_index, it->rule_order};
      }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", file.c_str());
    if (ImGui::IsItemHovered() || ImGui::IsItemHovered()) {
      std::string tip;
      for (const auto& d : it->rule->declarations) tip += d.property + ": " + d.value + ";\n";
      ImGui::SetItemTooltip("%s", tip.c_str());
    }
    ImGui::PopID();
  }
}

std::vector<StyleDeclaration> UIBuilder::InlineDeclarations(const UxmlNode& node) const {
  if (auto style = node.FindAttribute("style")) return ParseInlineStyle(*style);
  return {};
}

void UIBuilder::SetInlineDeclarations(UxmlNode& node,
                                      const std::vector<StyleDeclaration>& declarations) {
  std::string text;
  for (const auto& d : declarations) {
    text += (text.empty() ? "" : " ") + d.property + ": " + d.value + ";";
  }
  if (text.empty()) {
    node.RemoveAttribute("style");
  } else {
    node.SetAttribute("style", text);
  }
}

void UIBuilder::SetStyle(UxmlNode* node, StyleRule* rule, std::string_view property,
                         std::string_view value) {
  if (rule) {
    rule->SetDeclaration(property, value);
    sheets_[selection_.sheet].dirty = true;
  } else if (node) {
    auto decls = InlineDeclarations(*node);
    bool found = false;
    for (auto& d : decls) {
      if (d.property == property) {
        d.value = std::string(value);
        found = true;
      }
    }
    if (!found) decls.push_back({std::string(property), std::string(value), {}, 0});
    SetInlineDeclarations(*node, decls);
  }
  MarkChanged();
}

void UIBuilder::UnsetStyle(UxmlNode* node, StyleRule* rule, std::string_view property) {
  // Shorthands that set this property go too (margin for margin-left, ...).
  auto covers = [&](const StyleDeclaration& d) {
    if (d.property == property) return true;
    for (const auto& e : ExpandDeclaration(d.property, d.tokens)) {
      if (GetStylePropertyInfo(e.id).name == property) return true;
    }
    return false;
  };
  if (rule) {
    std::erase_if(rule->declarations, covers);
    sheets_[selection_.sheet].dirty = true;
  } else if (node) {
    auto decls = InlineDeclarations(*node);
    std::erase_if(decls, covers);
    SetInlineDeclarations(*node, decls);
  }
  MarkChanged();
}

void UIBuilder::ExtractInlineStylesToClass(UxmlNode& node, const std::string& class_name) {
  if (!ParseSelector("." + class_name)) {
    console_.Error("\"" + class_name + "\" is not a valid class name");
    return;
  }
  if (sheets_.empty()) {
    active_sheet_ = AddSheet(fs::path(doc_path_).stem().string() + ".uss");
  }
  auto& sheet = sheets_[active_sheet_];
  StyleRule* rule = sheet.sheet->FindRule("." + class_name);
  if (!rule) rule = &sheet.sheet->AddRule("." + class_name);
  for (const auto& d : InlineDeclarations(node)) rule->SetDeclaration(d.property, d.value);
  sheet.dirty = true;
  node.RemoveAttribute("style");
  std::string classes = node.FindAttribute("class") ? *node.FindAttribute("class") : "";
  node.SetAttribute("class", classes.empty() ? class_name : classes + " " + class_name);
  MarkChanged();
}

void UIBuilder::DrawStyleProperties(UxmlNode* node, StyleRule* rule) {
  // What this target declares, by longhand property (shorthands expanded).
  std::vector<StyleDeclaration> declarations =
      rule ? rule->declarations : (node ? InlineDeclarations(*node) : std::vector<StyleDeclaration>{});
  std::unordered_map<std::string, TokenList> variables;
  std::vector<std::optional<StyleValue>> declared(kStylePropertyCount);
  std::vector<std::string> declared_text(kStylePropertyCount);
  const std::string base = rule ? sheets_[selection_.sheet].res_path : doc_path_;
  for (const auto& d : declarations) {
    for (const auto& e : ExpandDeclaration(d.property, d.tokens)) {
      declared_text[static_cast<size_t>(e.id)] = TokensToString(e.tokens);
      TokenList tokens;
      if (!SubstituteVariables(e.tokens, variables, tokens)) continue;
      if (auto v = ParseStyleValue(e.id, tokens, base)) declared[static_cast<size_t>(e.id)] = *v;
    }
  }
  VisualElement* element = node ? ElementFor(node) : nullptr;

  for (const auto& group : StyleGroups()) {
    int set_count = 0;
    for (auto id : group.properties) set_count += declared[static_cast<size_t>(id)].has_value() ||
                                                  !declared_text[static_cast<size_t>(id)].empty();
    const std::string header = set_count ? std::format("{}  ({})###{}", group.name, set_count, group.name)
                                         : std::format("{}###{}", group.name, group.name);
    if (!ImGui::CollapsingHeader(header.c_str())) continue;
    ImGui::PushID(group.name);
    for (auto id : group.properties) {
      const size_t i = static_cast<size_t>(id);
      const char* name = GetStylePropertyInfo(id).name;
      ImGui::PushID(name);
      const bool is_set = declared[i].has_value() || !declared_text[i].empty();
      // Shown value: declared here, else what the element resolved to.
      StyleValue value = declared[i] ? *declared[i]
                         : element  ? element->resolvedStyle().Get(id)
                                    : DefaultStyleValue(id);
      ImGui::AlignTextToFramePadding();
      if (is_set) {
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", name);
      } else {
        ImGui::TextDisabled("%s", name);
      }
      if (is_set) {
        ImGui::SetItemTooltip("Set here: %s", declared_text[i].c_str());
      } else if (element) {
        ImGui::SetItemTooltip("Resolved: %s", FormatStyleValue(id, value).c_str());
      }
      ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.48f);
      ImGui::BeginGroup();
      bool changed = false;
      std::string text_value;
      if (IsListValue(value) || (!declared[i] && !declared_text[i].empty())) {
        // Lists and values using var() are edited as text.
        std::string text = !declared_text[i].empty() ? declared_text[i] : FormatStyleValue(id, value);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##text", &text, ImGuiInputTextFlags_EnterReturnsTrue)) {
          text_value = text;
          changed = true;
        }
      } else {
        changed = EditStyleValue(id, value);
        if (changed) text_value = FormatStyleValue(id, value);
      }
      ImGui::EndGroup();
      if (ImGui::BeginPopupContextItem("style_menu")) {
        if (ImGui::MenuItem("Unset", nullptr, false, is_set)) UnsetStyle(node, rule, name);
        ImGui::EndPopup();
      }
      if (changed && !text_value.empty()) SetStyle(node, rule, name, text_value);
      ImGui::PopID();
    }
    ImGui::PopID();
  }

  // Custom properties (--name) declared on a selector.
  if (rule) {
    ImGui::SeparatorText("Variables");
    int remove = -1;
    for (size_t i = 0; i < rule->declarations.size(); i++) {
      auto& d = rule->declarations[i];
      if (!d.property.starts_with("--")) continue;
      ImGui::PushID(static_cast<int>(i));
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(d.property.c_str());
      ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.48f);
      ImGui::SetNextItemWidth(-24);
      std::string text = d.value;
      if (ImGui::InputText("##v", &text, ImGuiInputTextFlags_EnterReturnsTrue)) {
        rule->SetDeclaration(d.property, text);
        sheets_[selection_.sheet].dirty = true;
        MarkChanged();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
      ImGui::PopID();
    }
    if (remove >= 0) {
      rule->declarations.erase(rule->declarations.begin() + remove);
      sheets_[selection_.sheet].dirty = true;
      MarkChanged();
    }
    static char var_name[64] = "";
    ImGui::SetNextItemWidth(-60);
    ImGui::InputTextWithHint("##newvar", "--my-color", var_name, sizeof(var_name));
    ImGui::SameLine();
    if (ImGui::Button("Add##var") && std::string(var_name).starts_with("--")) {
      rule->SetDeclaration(var_name, "0");
      sheets_[selection_.sheet].dirty = true;
      var_name[0] = '\0';
      MarkChanged();
    }
  }
}

// ------------------------------------------------------------------- Code ---

void UIBuilder::DrawCode() {
  if (ImGui::BeginTabBar("code_tabs")) {
    if (ImGui::BeginTabItem("UXML")) {
      std::string text = doc_->Write();
      ImGui::InputTextMultiline("##uxml", &text, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
      ImGui::EndTabItem();
    }
    for (size_t i = 0; i < sheets_.size(); i++) {
      const std::string tab = fs::path(sheets_[i].src).filename().string() + "###uss" + std::to_string(i);
      if (ImGui::BeginTabItem(tab.c_str())) {
        std::string text = sheets_[i].sheet->Write();
        ImGui::InputTextMultiline("##uss", &text, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
}

}  // namespace Mercury
