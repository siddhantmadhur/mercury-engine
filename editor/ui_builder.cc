#include "ui_builder.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "ui/controls.h"
#include "ui/style_resolver.h"

namespace Mercury {

namespace fs = std::filesystem;
using namespace UI;

namespace {

constexpr size_t kMaxUndoSteps = 200;

constexpr OverlayColor kSelectionColor{60, 150, 255, 255};
constexpr OverlayColor kHoverColor{120, 190, 255, 140};
constexpr OverlayColor kMarginColor{255, 160, 60, 60};
constexpr OverlayColor kPaddingColor{110, 200, 110, 60};

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

}  // namespace

UIBuilder::UIBuilder(Console& console) : console_(console) {}

UIBuilder::~UIBuilder() = default;

void UIBuilder::SetProject(std::shared_ptr<Project> project) {
  Close();
  project_ = std::move(project);
}

const std::vector<UIBuilder::StyleGroup>& UIBuilder::StyleGroups() {
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

bool UIBuilder::IsListValue(const StyleValue& v) {
  return std::holds_alternative<PropertyNameList>(v) || std::holds_alternative<TimeList>(v) ||
         std::holds_alternative<EasingList>(v);
}

std::string UIBuilder::NodeLabel(const UxmlNode& node) {
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
  hovered_node_ = nullptr;
  doc_revision_++;
  selection_revision_++;
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
  edit_open_ = false;
  selection_ = {};
  hovered_node_ = nullptr;
  drag_ = Handle::kNone;
  doc_revision_++;
  selection_revision_++;
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

void UIBuilder::SetActiveSheet(int index) {
  if (index >= 0 && index < static_cast<int>(sheets_.size()) && active_sheet_ != index) {
    active_sheet_ = index;
    selection_revision_++;
  }
}

int UIBuilder::AddSheet(const std::string& src) {
  if (!doc_) return 0;
  for (size_t i = 0; i < sheets_.size(); i++) {
    if (sheets_[i].src == src) return static_cast<int>(i);
  }
  BeginEdit();
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
    entry.dirty = true;
  }
  sheets_.push_back(std::move(entry));
  MarkChanged();
  CommitEdit();
  return static_cast<int>(sheets_.size()) - 1;
}

int UIBuilder::CreateNewSheetNextToDoc() {
  if (!doc_) return 0;
  const std::string stem = fs::path(doc_path_).stem().string();
  std::string name = stem + ".uss";
  for (int i = 2;
       project_ && fs::exists(project_->Resolve(ResolveRelativePath(doc_path_, name)));
       i++) {
    name = stem + std::to_string(i) + ".uss";
  }
  active_sheet_ = AddSheet(name);
  selection_revision_++;
  return active_sheet_;
}

void UIBuilder::RemoveSheet(int sheet_idx) {
  if (!doc_ || sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return;
  BeginEdit();
  std::erase(doc_->root.style_sheets, sheets_[sheet_idx].src);
  sheets_.erase(sheets_.begin() + sheet_idx);
  active_sheet_ = 0;
  selection_ = {Selection::Kind::kDocument};
  MarkChanged();
  CommitEdit();
  selection_revision_++;
}

bool UIBuilder::ValidateSelectorList(const std::string& text, std::string* error_out) {
  if (text.empty()) return false;
  for (size_t start = 0; start <= text.size();) {
    size_t comma = text.find(',', start);
    if (comma == std::string::npos) comma = text.size();
    if (!ParseSelector(text.substr(start, comma - start), error_out).has_value()) {
      return false;
    }
    start = comma + 1;
  }
  return true;
}

bool UIBuilder::AddRuleToActiveSheet(const std::string& selector_text, std::string* error_out) {
  if (!doc_ || !ValidateSelectorList(selector_text, error_out)) return false;
  BeginEdit();
  if (sheets_.empty()) {
    const std::string stem = fs::path(doc_path_).stem().string();
    doc_->root.style_sheets.push_back(stem + ".uss");
    SheetEntry entry;
    entry.src = stem + ".uss";
    entry.res_path = ResolveRelativePath(doc_path_, entry.src);
    std::string text;
    if (project_ && ReadFile(project_->Resolve(entry.res_path), text)) {
      entry.sheet = StyleSheet::Parse(text, entry.res_path);
    } else {
      entry.sheet = std::make_shared<StyleSheet>();
      entry.sheet->path = entry.res_path;
      entry.dirty = true;
    }
    sheets_.push_back(std::move(entry));
    active_sheet_ = 0;
  }
  auto& sheet = sheets_[active_sheet_];
  sheet.sheet->AddRule(selector_text);
  sheet.dirty = true;
  selection_ = {Selection::Kind::kSelector, {}, active_sheet_,
                static_cast<int>(sheet.sheet->rules.size()) - 1};
  MarkChanged();
  CommitEdit();
  selection_revision_++;
  return true;
}

void UIBuilder::DuplicateRule(int sheet_idx, int rule_idx) {
  if (sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return;
  auto& entry = sheets_[sheet_idx];
  if (rule_idx < 0 || rule_idx >= static_cast<int>(entry.sheet->rules.size())) return;
  BeginEdit();
  StyleRule copy = entry.sheet->rules[rule_idx];
  entry.sheet->rules.insert(entry.sheet->rules.begin() + rule_idx + 1, std::move(copy));
  entry.dirty = true;
  selection_ = {Selection::Kind::kSelector, {}, sheet_idx, rule_idx + 1};
  MarkChanged();
  CommitEdit();
  selection_revision_++;
}

void UIBuilder::DeleteRule(int sheet_idx, int rule_idx) {
  if (sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return;
  auto& entry = sheets_[sheet_idx];
  if (rule_idx < 0 || rule_idx >= static_cast<int>(entry.sheet->rules.size())) return;
  BeginEdit();
  entry.sheet->rules.erase(entry.sheet->rules.begin() + rule_idx);
  entry.dirty = true;
  if (selection_.kind == Selection::Kind::kSelector && selection_.sheet == sheet_idx &&
      selection_.rule == rule_idx) {
    selection_ = {Selection::Kind::kDocument};
    selection_revision_++;
  }
  MarkChanged();
  CommitEdit();
}

bool UIBuilder::RenameRule(StyleRule* rule, int sheet_idx, const std::string& selector_text) {
  if (!rule || sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return false;
  StyleRule renamed;
  bool ok = !selector_text.empty();
  for (size_t start = 0; ok && start <= selector_text.size();) {
    size_t comma = selector_text.find(',', start);
    if (comma == std::string::npos) comma = selector_text.size();
    auto parsed = ParseSelector(selector_text.substr(start, comma - start));
    if (!parsed) {
      ok = false;
    } else {
      renamed.selectors.push_back(*parsed);
    }
    start = comma + 1;
  }
  if (!ok || renamed.selectors.empty()) {
    console_.Error("\"" + selector_text + "\" is not a valid selector");
    return false;
  }
  BeginEdit();
  rule->selectors = std::move(renamed.selectors);
  sheets_[sheet_idx].dirty = true;
  MarkChanged();
  CommitEdit();
  return true;
}

std::string UIBuilder::RelativeToDocument(const std::string& res_path) const {
  if (!res_path.starts_with("res://") || !doc_path_.starts_with("res://")) return res_path;
  const fs::path target(res_path.substr(6));
  const fs::path base = fs::path(doc_path_.substr(6)).parent_path();
  return target.lexically_relative(base).generic_string();
}

std::string UIBuilder::WriteUxml() const { return doc_ ? doc_->Write() : ""; }

std::string UIBuilder::WriteSheet(size_t index) const {
  return index < sheets_.size() && sheets_[index].sheet ? sheets_[index].sheet->Write() : "";
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
  hovered_node_ = nullptr;
  dirty_ = true;
  preview_dirty_ = true;
  doc_revision_++;
  selection_revision_++;
}

void UIBuilder::BeginEdit() {
  if (!edit_open_ && doc_) {
    last_committed_ = TakeSnapshot();
    edit_open_ = true;
  }
}

void UIBuilder::CommitEdit() {
  if (!edit_open_ || !doc_) return;
  Snapshot current = TakeSnapshot();
  if (current.uxml != last_committed_.uxml || current.sheets != last_committed_.sheets) {
    undo_.push_back(std::move(last_committed_));
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    last_committed_ = std::move(current);
  }
  edit_open_ = false;
}

void UIBuilder::MarkChanged(bool coalesced) {
  if (!edit_open_) {
    undo_.push_back(last_committed_);
    if (undo_.size() > kMaxUndoSteps) undo_.erase(undo_.begin());
    redo_.clear();
    if (coalesced) {
      edit_open_ = true;
    } else {
      last_committed_ = TakeSnapshot();
    }
  }
  dirty_ = true;
  preview_dirty_ = true;
  doc_revision_++;
}

void UIBuilder::Undo() {
  if (!doc_) return;
  CommitEdit();
  if (undo_.empty()) return;
  redo_.push_back(last_committed_);
  last_committed_ = undo_.back();
  undo_.pop_back();
  RestoreSnapshot(last_committed_);
}

void UIBuilder::Redo() {
  if (!doc_) return;
  CommitEdit();
  if (redo_.empty()) return;
  undo_.push_back(last_committed_);
  last_committed_ = redo_.back();
  redo_.pop_back();
  RestoreSnapshot(last_committed_);
}

bool UIBuilder::Save() {
  if (!doc_ || !project_) return false;
  CommitEdit();
  bool ok = WriteFile(project_->Resolve(doc_path_), doc_->Write());
  if (!ok) console_.Error("UI Builder: could not write " + doc_path_);
  for (auto& sheet : sheets_) {
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
    doc_revision_++;
  }
  return ok;
}

void UIBuilder::Select(Selection selection) {
  if (selection_ != selection) {
    CommitEdit();
    selection_ = std::move(selection);
    if (selection_.kind == Selection::Kind::kSelector && selection_.sheet >= 0 &&
        selection_.sheet < static_cast<int>(sheets_.size())) {
      active_sheet_ = selection_.sheet;
    }
    selection_revision_++;
  }
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
  BeginEdit();
  UxmlNode* parent = NodeAt(parent_path);
  if (!parent) return;
  index = std::clamp(index, 0, static_cast<int>(parent->children.size()));
  parent->children.insert(parent->children.begin() + index, std::move(node));
  selection_.kind = Selection::Kind::kElement;
  selection_.element = parent_path;
  selection_.element.push_back(index);
  hovered_node_ = nullptr;
  MarkChanged();
  CommitEdit();
  selection_revision_++;
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
  hovered_node_ = nullptr;
  MarkChanged(true);
  return node;
}

void UIBuilder::MoveNode(const std::vector<int>& from, const std::vector<int>& parent_path,
                         int index) {
  if (parent_path.size() >= from.size() &&
      std::equal(from.begin(), from.end(), parent_path.begin())) {
    return;
  }
  BeginEdit();
  std::vector<int> target = parent_path;
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
  if (!node) {
    CommitEdit();
    return;
  }
  UxmlNode* parent = NodeAt(target);
  if (!parent) {
    CommitEdit();
    return;
  }
  index = std::clamp(index, 0, static_cast<int>(parent->children.size()));
  parent->children.insert(parent->children.begin() + index, std::move(*node));
  selection_.kind = Selection::Kind::kElement;
  selection_.element = target;
  selection_.element.push_back(index);
  hovered_node_ = nullptr;
  MarkChanged();
  CommitEdit();
  selection_revision_++;
}

void UIBuilder::WrapInVisualElement(const std::vector<int>& path) {
  if (path.empty()) return;
  BeginEdit();
  auto removed = RemoveNode(path);
  if (!removed) {
    CommitEdit();
    return;
  }
  std::vector<int> parent(path.begin(), path.end() - 1);
  UxmlNode wrapper = NewNode("VisualElement");
  wrapper.children.push_back(std::move(*removed));
  InsertNode(parent, path.back(), std::move(wrapper));
}

void UIBuilder::UnwrapElement(const std::vector<int>& path) {
  if (path.empty()) return;
  UxmlNode* node = NodeAt(path);
  if (!node || node->children.empty()) return;
  BeginEdit();
  auto removed = RemoveNode(path);
  if (!removed) {
    CommitEdit();
    return;
  }
  std::vector<int> parent_path(path.begin(), path.end() - 1);
  UxmlNode* parent = NodeAt(parent_path);
  if (!parent) {
    CommitEdit();
    return;
  }
  int index = std::clamp(path.back(), 0, static_cast<int>(parent->children.size()));
  for (size_t i = 0; i < removed->children.size(); i++) {
    parent->children.insert(parent->children.begin() + index + static_cast<int>(i),
                            std::move(removed->children[i]));
  }
  selection_ = {Selection::Kind::kElement, parent_path};
  selection_.element.push_back(index);
  hovered_node_ = nullptr;
  MarkChanged();
  CommitEdit();
  selection_revision_++;
}

void UIBuilder::DeleteSelected() {
  if (selection_.kind == Selection::Kind::kElement) {
    BeginEdit();
    RemoveNode(selection_.element);
    selection_ = {Selection::Kind::kDocument};
    MarkChanged();
    CommitEdit();
    selection_revision_++;
  } else if (selection_.kind == Selection::Kind::kSelector && SelectedRule()) {
    DeleteRule(selection_.sheet, selection_.rule);
  }
}

void UIBuilder::DuplicateSelected() {
  UxmlNode* node = SelectedNode();
  if (!node || selection_.element.empty()) return;
  UxmlNode copy = *node;
  if (auto name = copy.FindAttribute("name"); name && !name->empty()) {
    copy.SetAttribute("name", *name + "-copy");
  }
  std::vector<int> parent(selection_.element.begin(), selection_.element.end() - 1);
  InsertNode(parent, selection_.element.back() + 1, std::move(copy));
}

void UIBuilder::RemoveTemplate(size_t index) {
  if (!doc_ || index >= doc_->templates.size()) return;
  BeginEdit();
  doc_->templates.erase(doc_->templates.begin() + static_cast<ptrdiff_t>(index));
  MarkChanged();
  CommitEdit();
}

// ------------------------------------------------------------------ Style ---

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

void UIBuilder::ResolveDeclaredStyles(const UxmlNode* node, const StyleRule* rule,
                                      std::vector<std::optional<StyleValue>>& declared,
                                      std::vector<std::string>& declared_text) const {
  declared.assign(kStylePropertyCount, std::nullopt);
  declared_text.assign(kStylePropertyCount, "");
  std::vector<StyleDeclaration> declarations =
      rule ? rule->declarations : (node ? InlineDeclarations(*node) : std::vector<StyleDeclaration>{});
  std::unordered_map<std::string, TokenList> variables;
  const std::string base = (rule && selection_.sheet >= 0 &&
                            selection_.sheet < static_cast<int>(sheets_.size()))
                               ? sheets_[selection_.sheet].res_path
                               : doc_path_;
  for (const auto& d : declarations) {
    for (const auto& e : ExpandDeclaration(d.property, d.tokens)) {
      declared_text[static_cast<size_t>(e.id)] = TokensToString(e.tokens);
      TokenList tokens;
      if (!SubstituteVariables(e.tokens, variables, tokens)) continue;
      if (auto v = ParseStyleValue(e.id, tokens, base)) declared[static_cast<size_t>(e.id)] = *v;
    }
  }
}

void UIBuilder::SetStyle(UxmlNode* node, StyleRule* rule, std::string_view property,
                         std::string_view value, bool coalesced) {
  if (!coalesced) BeginEdit();
  if (rule && selection_.sheet >= 0 && selection_.sheet < static_cast<int>(sheets_.size())) {
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
  MarkChanged(coalesced);
  if (!coalesced) CommitEdit();
}

void UIBuilder::UnsetStyle(UxmlNode* node, StyleRule* rule, std::string_view property) {
  auto covers = [&](const StyleDeclaration& d) {
    if (d.property == property) return true;
    for (const auto& e : ExpandDeclaration(d.property, d.tokens)) {
      if (GetStylePropertyInfo(e.id).name == property) return true;
    }
    return false;
  };
  BeginEdit();
  if (rule && selection_.sheet >= 0 && selection_.sheet < static_cast<int>(sheets_.size())) {
    std::erase_if(rule->declarations, covers);
    sheets_[selection_.sheet].dirty = true;
  } else if (node) {
    auto decls = InlineDeclarations(*node);
    std::erase_if(decls, covers);
    SetInlineDeclarations(*node, decls);
  }
  MarkChanged();
  CommitEdit();
}

void UIBuilder::SetVariable(StyleRule* rule, int sheet_idx, const std::string& property,
                            const std::string& value, bool coalesced) {
  if (!rule || sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return;
  if (!coalesced) BeginEdit();
  rule->SetDeclaration(property, value);
  sheets_[sheet_idx].dirty = true;
  MarkChanged(coalesced);
  if (!coalesced) CommitEdit();
}

void UIBuilder::RemoveVariable(StyleRule* rule, int sheet_idx, size_t decl_index) {
  if (!rule || sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return;
  if (decl_index >= rule->declarations.size()) return;
  BeginEdit();
  rule->declarations.erase(rule->declarations.begin() + static_cast<ptrdiff_t>(decl_index));
  sheets_[sheet_idx].dirty = true;
  MarkChanged();
  CommitEdit();
}

bool UIBuilder::AddVariable(StyleRule* rule, int sheet_idx, const std::string& property) {
  if (!rule || sheet_idx < 0 || sheet_idx >= static_cast<int>(sheets_.size())) return false;
  if (!property.starts_with("--") || property.size() <= 2) return false;
  BeginEdit();
  rule->SetDeclaration(property, "0");
  sheets_[sheet_idx].dirty = true;
  MarkChanged();
  CommitEdit();
  return true;
}

void UIBuilder::ExtractInlineStylesToClass(UxmlNode& node, const std::string& class_name) {
  std::string clean = class_name;
  if (clean.starts_with('.')) clean = clean.substr(1);
  if (clean.empty() || !ParseSelector("." + clean)) {
    console_.Error("\"" + class_name + "\" is not a valid class name");
    return;
  }
  BeginEdit();
  if (sheets_.empty()) {
    const std::string stem = fs::path(doc_path_).stem().string();
    doc_->root.style_sheets.push_back(stem + ".uss");
    SheetEntry entry;
    entry.src = stem + ".uss";
    entry.res_path = ResolveRelativePath(doc_path_, entry.src);
    std::string text;
    if (project_ && ReadFile(project_->Resolve(entry.res_path), text)) {
      entry.sheet = StyleSheet::Parse(text, entry.res_path);
    } else {
      entry.sheet = std::make_shared<StyleSheet>();
      entry.sheet->path = entry.res_path;
      entry.dirty = true;
    }
    sheets_.push_back(std::move(entry));
    active_sheet_ = 0;
  }
  auto& sheet = sheets_[active_sheet_];
  StyleRule* rule = sheet.sheet->FindRule("." + clean);
  if (!rule) rule = &sheet.sheet->AddRule("." + clean);
  for (const auto& d : InlineDeclarations(node)) rule->SetDeclaration(d.property, d.value);
  sheet.dirty = true;
  node.RemoveAttribute("style");
  std::string classes = node.FindAttribute("class") ? *node.FindAttribute("class") : "";
  node.SetAttribute("class", classes.empty() ? clean : classes + " " + clean);
  MarkChanged();
  CommitEdit();
}

std::vector<UIBuilder::MatchingRuleInfo> UIBuilder::MatchingSelectors(
    VisualElement* element) const {
  std::vector<MatchingRuleInfo> result;
  if (!element || !resources_ || !panel_) return result;
  std::vector<const StyleSheet*> panel_sheets;
  if (auto theme =
          theme_.empty() ? resources_->DefaultTheme() : resources_->LoadStyleSheet(theme_)) {
    resources_->CollectSheets(theme, panel_sheets);
  }
  const auto sheets = ApplicableSheets(element, *resources_, panel_sheets);
  const auto matches = MatchRules(element, sheets);
  for (auto it = matches.rbegin(); it != matches.rend(); ++it) {
    MatchingRuleInfo info;
    info.selector_text = it->selector->text;
    info.sheet_file = fs::path(it->sheet->path).filename().string();
    for (size_t s = 0; s < sheets_.size(); s++) {
      if (sheets_[s].sheet.get() == it->sheet) info.sheet_index = static_cast<int>(s);
    }
    info.rule_order = it->rule_order;
    for (const auto& d : it->rule->declarations) {
      info.tooltip += d.property + ": " + d.value + ";\n";
    }
    result.push_back(std::move(info));
  }
  return result;
}

void UIBuilder::SetAttribute(UxmlNode& node, const std::string& name, const std::string& value,
                             const std::string& default_value, bool coalesced) {
  if (!coalesced) BeginEdit();
  if (value == default_value && name != "text") {
    node.RemoveAttribute(name);
  } else {
    node.SetAttribute(name, value);
  }
  MarkChanged(coalesced);
  if (!coalesced) CommitEdit();
}

void UIBuilder::UnsetAttribute(UxmlNode& node, const std::string& name) {
  BeginEdit();
  node.RemoveAttribute(name);
  MarkChanged();
  CommitEdit();
}

void UIBuilder::SetClassList(UxmlNode& node, const std::vector<std::string>& classes) {
  BeginEdit();
  std::string joined;
  for (const auto& c : classes) joined += (joined.empty() ? "" : " ") + c;
  if (joined.empty()) {
    node.RemoveAttribute("class");
  } else {
    node.SetAttribute("class", joined);
  }
  MarkChanged();
  CommitEdit();
}

void UIBuilder::AddBinding(UxmlNode& node) {
  BeginEdit();
  const UxmlTypeInfo* type = UxmlFactoryRegistry::Instance().Find(node.type);
  const std::string prop =
      node.FindAttribute("value") || (type && type->name != "Label" && type->name != "Button")
          ? "value"
          : "text";
  node.bindings.push_back({prop, "", BindingMode::kToTarget});
  MarkChanged();
  CommitEdit();
}

void UIBuilder::RemoveBinding(UxmlNode& node, size_t idx) {
  if (idx >= node.bindings.size()) return;
  BeginEdit();
  node.bindings.erase(node.bindings.begin() + static_cast<ptrdiff_t>(idx));
  MarkChanged();
  CommitEdit();
}

void UIBuilder::UpdateBinding(UxmlNode& node, size_t idx, const UI::UxmlBindingDesc& binding,
                              bool coalesced) {
  if (idx >= node.bindings.size()) return;
  if (!coalesced) BeginEdit();
  node.bindings[idx] = binding;
  MarkChanged(coalesced);
  if (!coalesced) CommitEdit();
}

// ---------------------------------------------------------------- Preview ---

void UIBuilder::RebuildPreview() {
  preview_dirty_ = false;
  preview_root_ = nullptr;
  hovered_node_ = nullptr;
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
  resources_->Reload();
  for (const auto& sheet : sheets_) resources_->Override(sheet.res_path, sheet.sheet);

  panel_ = std::make_unique<Panel>(resources_.get());
  PanelSettings settings;
  settings.scale = zoom_;
  settings.theme_style_sheet = theme_;
  panel_->SetSettings(settings);
  if (clipboard_get) panel_->clipboard.get = clipboard_get;
  if (clipboard_set) panel_->clipboard.set = clipboard_set;
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

void UIBuilder::UpdatePreview(RenderHost* host, float dt) {
  if (preview_dirty_ || !panel_) RebuildPreview();
  if (!panel_) {
    if (host) host->SetUIPreview(nullptr, 1, 1, background_);
    return;
  }
  PanelSettings settings = panel_->settings();
  if (settings.scale != zoom_ || settings.theme_style_sheet != theme_) {
    settings.scale = zoom_;
    settings.theme_style_sheet = theme_;
    panel_->SetSettings(settings);
  }
  const glm::vec2 pixels = glm::max(glm::vec2(canvas_size_) * zoom_, glm::vec2(1.0f));
  panel_->Update(dt, glm::round(pixels));
  if (host) {
    host->SetUIPreview(&panel_->drawData(), static_cast<int>(std::round(pixels.x)),
                       static_cast<int>(std::round(pixels.y)), background_);
  }
}

void UIBuilder::DispatchPreviewInput(const UIInputFrame& frame) {
  if (panel_) panel_->DispatchInput(frame);
}

void UIBuilder::SetCanvasSize(glm::ivec2 size, bool fit) {
  size.x = std::clamp(size.x, 16, 8192);
  size.y = std::clamp(size.y, 16, 8192);
  canvas_size_ = size;
  if (fit) fit_on_open_ = true;
}

void UIBuilder::SetZoom(float zoom) { zoom_ = std::clamp(zoom, 0.1f, 4.0f); }

void UIBuilder::SetPan(glm::vec2 pan) { pan_ = pan; }

void UIBuilder::SetBackground(glm::vec4 bg) { background_ = bg; }

void UIBuilder::SetTheme(const std::string& theme) { theme_ = theme; }

void UIBuilder::SetPreviewMode(bool preview) {
  if (preview_mode_ != preview) {
    preview_mode_ = preview;
    drag_ = Handle::kNone;
    hovered_node_ = nullptr;
    selection_revision_++;
  }
}

bool UIBuilder::FitIfRequested(glm::vec2 viewport_size) {
  if (!fit_on_open_ || viewport_size.x < 8.0f || viewport_size.y < 8.0f) return false;
  FitToViewport(viewport_size);
  fit_on_open_ = false;
  return true;
}

void UIBuilder::FitToViewport(glm::vec2 viewport_size) {
  if (viewport_size.x < 8.0f || viewport_size.y < 8.0f) return;
  zoom_ = std::min((viewport_size.x - 40.0f) / static_cast<float>(canvas_size_.x),
                   (viewport_size.y - 40.0f) / static_cast<float>(canvas_size_.y));
  zoom_ = std::clamp(zoom_, 0.1f, 4.0f);
  pan_ = {0.0f, 0.0f};
}

void UIBuilder::ResetZoom1To1() {
  zoom_ = 1.0f;
  pan_ = {0.0f, 0.0f};
}

glm::vec2 UIBuilder::ComputeCanvasOrigin(glm::vec2 viewport_size) const {
  const glm::vec2 canvas_px(std::round(canvas_size_.x * zoom_),
                            std::round(canvas_size_.y * zoom_));
  return glm::vec2(std::round((viewport_size.x - canvas_px.x) * 0.5f + pan_.x),
                   std::round((viewport_size.y - canvas_px.y) * 0.5f + pan_.y));
}

VisualElement* UIBuilder::PickForEditing(glm::vec2 point) const {
  if (!preview_root_) return nullptr;
  VisualElement* found = nullptr;
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    if (e->resolvedStyle().display == DisplayStyle::kNone) return;
    if (e->uxmlSource && e != preview_root_ && e->worldBound().Contains(point)) found = e;
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(preview_root_);
  return found;
}

UIBuilder::Handle UIBuilder::HitTestHandle(glm::vec2 local_mouse, glm::vec2 origin) const {
  UxmlNode* node = const_cast<UIBuilder*>(this)->SelectedNode();
  VisualElement* element = ElementFor(node);
  if (!element) return Handle::kNone;
  const Rect b = element->worldBound();
  const glm::vec2 min(origin.x + b.x * zoom_, origin.y + b.y * zoom_);
  const glm::vec2 max(origin.x + b.xMax() * zoom_, origin.y + b.yMax() * zoom_);
  auto near_pt = [&](glm::vec2 p) {
    return std::abs(local_mouse.x - p.x) <= 6.0f && std::abs(local_mouse.y - p.y) <= 6.0f;
  };
  if (near_pt(max)) return Handle::kCorner;
  if (near_pt(glm::vec2(max.x, (min.y + max.y) * 0.5f))) return Handle::kRight;
  if (near_pt(glm::vec2((min.x + max.x) * 0.5f, max.y))) return Handle::kBottom;
  if (local_mouse.x >= min.x && local_mouse.y >= min.y && local_mouse.x <= max.x &&
      local_mouse.y <= max.y && element->resolvedStyle().position == Position::kAbsolute) {
    return Handle::kMove;
  }
  return Handle::kNone;
}

void UIBuilder::OnCanvasWheel(float wheel_delta, float wheel_h, bool ctrl_down) {
  if (ctrl_down && wheel_delta != 0.0f) {
    const float old_zoom = zoom_;
    zoom_ = std::clamp(zoom_ * std::pow(1.15f, wheel_delta), 0.1f, 4.0f);
    const float ratio = zoom_ / old_zoom;
    pan_ *= ratio;
  } else if (wheel_delta != 0.0f || wheel_h != 0.0f) {
    pan_.y += wheel_delta * 40.0f;
    pan_.x += wheel_h * 40.0f;
  }
}

void UIBuilder::OnCanvasMouseDown(glm::vec2 local_mouse, glm::vec2 origin, int button,
                                  bool alt_down) {
  drag_start_mouse_ = local_mouse;
  if (button == 1 || alt_down) {
    drag_ = Handle::kPan;
    return;
  }
  if (button != 0) return;

  const glm::vec2 point((local_mouse.x - origin.x) / zoom_, (local_mouse.y - origin.y) / zoom_);
  VisualElement* hover = PickForEditing(point);
  UxmlNode* node = SelectedNode();
  drag_ = HitTestHandle(local_mouse, origin);
  if (drag_ == Handle::kNone ||
      (drag_ == Handle::kMove && hover && hover->uxmlSource != node)) {
    if (hover) {
      if (auto path = PathOf(static_cast<const UxmlNode*>(hover->uxmlSource))) {
        Select({Selection::Kind::kElement, *path});
      }
    } else {
      Select({Selection::Kind::kDocument});
    }
    drag_ = Handle::kNone;
    VisualElement* element = ElementFor(SelectedNode());
    if (element && element->resolvedStyle().position == Position::kAbsolute) {
      drag_ = Handle::kMove;
    }
  }
  VisualElement* element = ElementFor(SelectedNode());
  if (element) {
    drag_start_size_ = {element->layout().width, element->layout().height};
    drag_start_pos_ = {element->layout().x, element->layout().y};
    BeginEdit();
  }
}

void UIBuilder::OnCanvasMouseDrag(glm::vec2 local_mouse, glm::vec2 mouse_delta) {
  if (drag_ == Handle::kNone) return;
  if (drag_ == Handle::kPan) {
    pan_ += mouse_delta;
    return;
  }
  UxmlNode* node = SelectedNode();
  const glm::vec2 delta = local_mouse - drag_start_mouse_;
  if (!node || (delta.x == 0.0f && delta.y == 0.0f)) return;
  auto px = [](float v) { return std::format("{}px", static_cast<int>(std::round(v))); };
  switch (drag_) {
    case Handle::kRight:
      SetStyle(node, nullptr, "width", px(drag_start_size_.x + delta.x / zoom_), true);
      break;
    case Handle::kBottom:
      SetStyle(node, nullptr, "height", px(drag_start_size_.y + delta.y / zoom_), true);
      break;
    case Handle::kCorner:
      SetStyle(node, nullptr, "width", px(drag_start_size_.x + delta.x / zoom_), true);
      SetStyle(node, nullptr, "height", px(drag_start_size_.y + delta.y / zoom_), true);
      break;
    case Handle::kMove:
      if (std::abs(delta.x) + std::abs(delta.y) < 3.0f) return;
      SetStyle(node, nullptr, "left", px(drag_start_pos_.x + delta.x / zoom_), true);
      SetStyle(node, nullptr, "top", px(drag_start_pos_.y + delta.y / zoom_), true);
      break;
    default:
      break;
  }
}

void UIBuilder::OnCanvasMouseUp() {
  if (drag_ != Handle::kNone && drag_ != Handle::kPan) CommitEdit();
  drag_ = Handle::kNone;
}

void UIBuilder::UpdateHoveredElement(glm::vec2 local_mouse, glm::vec2 origin) {
  const glm::vec2 point((local_mouse.x - origin.x) / zoom_, (local_mouse.y - origin.y) / zoom_);
  VisualElement* hover = PickForEditing(point);
  hovered_node_ = hover ? static_cast<const UxmlNode*>(hover->uxmlSource) : nullptr;
}

void UIBuilder::SetHoveredNode(const UxmlNode* node) { hovered_node_ = node; }

void UIBuilder::RenderCanvasOverlay(OverlayCanvas& draw, glm::vec2 origin) const {
  auto to_screen = [&](float x, float y) {
    return glm::vec2(origin.x + x * zoom_, origin.y + y * zoom_);
  };
  UxmlNode* selected_node = const_cast<UIBuilder*>(this)->SelectedNode();
  if (hovered_node_ && hovered_node_ != selected_node) {
    if (auto* hover = ElementFor(hovered_node_)) {
      const Rect b = hover->worldBound();
      draw.AddRect(to_screen(b.x, b.y), to_screen(b.xMax(), b.yMax()), kHoverColor, 0.0f, 1.0f);
    }
  }

  VisualElement* element = ElementFor(selected_node);
  if (!element) return;
  const Rect b = element->worldBound();
  const ComputedStyle& s = element->resolvedStyle();
  const float parent_width =
      element->hierarchyParent() ? element->hierarchyParent()->layout().width : 0.0f;
  const float ml = s.marginLeft.Resolve(parent_width), mr = s.marginRight.Resolve(parent_width);
  const float mt = s.marginTop.Resolve(parent_width), mb = s.marginBottom.Resolve(parent_width);
  const glm::vec2 outer_min = to_screen(b.x - ml, b.y - mt);
  const glm::vec2 outer_max = to_screen(b.xMax() + mr, b.yMax() + mb);
  const glm::vec2 min = to_screen(b.x, b.y);
  const glm::vec2 max = to_screen(b.xMax(), b.yMax());
  draw.AddRectFilled(outer_min, glm::vec2(outer_max.x, min.y), kMarginColor);
  draw.AddRectFilled(glm::vec2(outer_min.x, max.y), outer_max, kMarginColor);
  draw.AddRectFilled(glm::vec2(outer_min.x, min.y), glm::vec2(min.x, max.y), kMarginColor);
  draw.AddRectFilled(glm::vec2(max.x, min.y), glm::vec2(outer_max.x, max.y), kMarginColor);

  const Rect content = element->contentRect();
  const glm::vec2 cmin = to_screen(b.x + content.x, b.y + content.y);
  const glm::vec2 cmax = to_screen(b.x + content.xMax(), b.y + content.yMax());
  draw.AddRectFilled(min, glm::vec2(max.x, cmin.y), kPaddingColor);
  draw.AddRectFilled(glm::vec2(min.x, cmax.y), max, kPaddingColor);
  draw.AddRectFilled(glm::vec2(min.x, cmin.y), glm::vec2(cmin.x, cmax.y), kPaddingColor);
  draw.AddRectFilled(glm::vec2(cmax.x, cmin.y), glm::vec2(max.x, cmax.y), kPaddingColor);

  draw.AddRect(min, max, kSelectionColor, 0.0f, 2.0f);
  constexpr OverlayColor kWhite{255, 255, 255, 255};
  for (glm::vec2 h :
       {max, glm::vec2(max.x, (min.y + max.y) * 0.5f), glm::vec2((min.x + max.x) * 0.5f, max.y)}) {
    draw.AddRectFilled(glm::vec2(h.x - 4.0f, h.y - 4.0f), glm::vec2(h.x + 4.0f, h.y + 4.0f),
                       kWhite);
    draw.AddRect(glm::vec2(h.x - 4.0f, h.y - 4.0f), glm::vec2(h.x + 4.0f, h.y + 4.0f),
                 kSelectionColor);
  }
  const std::string size =
      std::format("{} x {}", static_cast<int>(b.width), static_cast<int>(b.height));
  draw.AddText(glm::vec2(min.x, max.y + 4.0f), kSelectionColor, size);
}

void UIBuilder::DropNewTypeAtPoint(const std::string& type_name, glm::vec2 local_mouse,
                                   glm::vec2 origin) {
  if (!doc_) return;
  const glm::vec2 pt((local_mouse.x - origin.x) / zoom_, (local_mouse.y - origin.y) / zoom_);
  VisualElement* target = PickForEditing(pt);
  std::vector<int> parent;
  if (target) {
    if (auto p = PathOf(static_cast<const UxmlNode*>(target->uxmlSource))) parent = *p;
  }
  UxmlNode* parent_node = NodeAt(parent);
  const int index = parent_node ? static_cast<int>(parent_node->children.size()) : 0;
  InsertNode(parent, index, NewNode(type_name));
}

void UIBuilder::DropTemplateAtPoint(const std::string& template_res_path, glm::vec2 local_mouse,
                                    glm::vec2 origin) {
  if (!doc_) return;
  const glm::vec2 pt((local_mouse.x - origin.x) / zoom_, (local_mouse.y - origin.y) / zoom_);
  VisualElement* target = PickForEditing(pt);
  std::vector<int> parent;
  if (target) {
    if (auto p = PathOf(static_cast<const UxmlNode*>(target->uxmlSource))) parent = *p;
  }
  UxmlNode* parent_node = NodeAt(parent);
  const int index = parent_node ? static_cast<int>(parent_node->children.size()) : 0;
  std::string name = fs::path(template_res_path).stem().string();
  BeginEdit();
  if (!doc_->FindTemplate(name)) {
    doc_->templates.push_back({name, RelativeToDocument(template_res_path)});
  }
  UxmlNode instance;
  instance.type = "Instance";
  instance.attributes.push_back({"template", name});
  InsertNode(parent, index, std::move(instance));
}

}  // namespace Mercury
