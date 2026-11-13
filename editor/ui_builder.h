/*
 * ui_builder.h
 *
 * Controller for the UI Builder, modeled on Unity's UI Builder: edit a .uxml
 * document and its .uss style sheets visually.
 *
 *   StyleSheets   the document's sheets and their selectors
 *   Hierarchy     the document's elements (drag to reorder or reparent)
 *   Library       controls and project templates to drag in
 *   Viewport      the live document, rendered by the engine's UI renderer;
 *                 click to select, drag handles to resize, Preview to try it
 *   Inspector     attributes, classes, bindings and styles of the selection
 *                 (inline on an element, or the rule of a selected selector)
 *   Code          the UXML and USS being written
 */
#ifndef MERCURY_EDITOR_UI_BUILDER_H
#define MERCURY_EDITOR_UI_BUILDER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "glm/vec2.hpp"
#include "glm/vec4.hpp"
#include "overlay_canvas.h"
#include "project/project.h"
#include "scene/scene.h"
#include "ui/panel.h"
#include "ui/uxml.h"

namespace Mercury {

class UIBuilder {
 public:
  struct SheetEntry {
    std::string src;       // as written in <Style src="...">
    std::string res_path;  // resolved
    std::shared_ptr<UI::StyleSheet> sheet;
    bool dirty = false;
  };

  struct Selection {
    enum class Kind { kNone, kDocument, kElement, kSelector };
    Kind kind = Kind::kNone;
    std::vector<int> element;  // child indices from the document root
    int sheet = -1;
    int rule = -1;
    bool operator==(const Selection&) const = default;
  };

  struct StyleGroup {
    const char* name;
    std::vector<UI::StylePropertyId> properties;
  };

  struct MatchingRuleInfo {
    std::string selector_text;
    std::string sheet_file;
    int sheet_index = -1;
    int rule_order = -1;
    std::string tooltip;
  };

  enum class Handle { kNone, kMove, kRight, kBottom, kCorner, kPan };

  explicit UIBuilder(Console& console);
  ~UIBuilder();

  void SetProject(std::shared_ptr<Project> project);

  bool Open(const std::string& res_path);
  void Close();
  bool IsOpen() const { return doc_ != nullptr; }
  const std::string& path() const { return doc_path_; }
  bool IsDirty() const { return dirty_; }
  bool Save();

  void Undo();
  void Redo();
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }

  void BeginEdit();
  void CommitEdit();

  uint64_t DocRevision() const { return doc_revision_; }
  uint64_t SelectionRevision() const { return selection_revision_; }

  // Callbacks provided by the UI host.
  std::function<std::vector<std::string>(const std::vector<std::string>&)> list_files;
  std::function<std::string()> clipboard_get;
  std::function<void(const std::string&)> clipboard_set;

  // -------------------------------------------------------------- Document
  std::shared_ptr<UI::VisualTreeAsset> Doc() const { return doc_; }
  const std::vector<SheetEntry>& Sheets() const { return sheets_; }
  std::vector<SheetEntry>& Sheets() { return sheets_; }
  int ActiveSheetIndex() const { return active_sheet_; }
  void SetActiveSheet(int index);

  int AddSheet(const std::string& src);
  int CreateNewSheetNextToDoc();
  void RemoveSheet(int sheet_idx);

  bool AddRuleToActiveSheet(const std::string& selector_text, std::string* error_out = nullptr);
  void DuplicateRule(int sheet_idx, int rule_idx);
  void DeleteRule(int sheet_idx, int rule_idx);
  bool RenameRule(UI::StyleRule* rule, int sheet_idx, const std::string& selector_text);

  static bool ValidateSelectorList(const std::string& text, std::string* error_out = nullptr);
  std::string RelativeToDocument(const std::string& res_path) const;

  std::string WriteUxml() const;
  std::string WriteSheet(size_t index) const;

  // ------------------------------------------------------------- Selection
  const Selection& CurrentSelection() const { return selection_; }
  void Select(Selection selection);
  UI::UxmlNode* SelectedNode();
  UI::StyleRule* SelectedRule();
  UI::UxmlNode* NodeAt(const std::vector<int>& path);
  std::optional<std::vector<int>> PathOf(const UI::UxmlNode* node) const;
  UI::VisualElement* ElementFor(const UI::UxmlNode* node) const;
  static std::string NodeLabel(const UI::UxmlNode& node);

  // ------------------------------------------------------------- Hierarchy
  UI::UxmlNode NewNode(const std::string& type) const;
  void InsertNode(const std::vector<int>& parent, int index, UI::UxmlNode node);
  std::optional<UI::UxmlNode> RemoveNode(const std::vector<int>& path);
  void MoveNode(const std::vector<int>& from, const std::vector<int>& parent, int index);
  void WrapInVisualElement(const std::vector<int>& path);
  void UnwrapElement(const std::vector<int>& path);
  void DeleteSelected();
  void DuplicateSelected();
  void RemoveTemplate(size_t index);

  // --------------------------------------------------------- Attributes & Style
  static const std::vector<StyleGroup>& StyleGroups();
  static bool IsListValue(const UI::StyleValue& v);

  std::vector<UI::StyleDeclaration> InlineDeclarations(const UI::UxmlNode& node) const;
  void ResolveDeclaredStyles(const UI::UxmlNode* node, const UI::StyleRule* rule,
                             std::vector<std::optional<UI::StyleValue>>& declared,
                             std::vector<std::string>& declared_text) const;

  void SetStyle(UI::UxmlNode* node, UI::StyleRule* rule, std::string_view property,
                std::string_view value, bool coalesced = false);
  void UnsetStyle(UI::UxmlNode* node, UI::StyleRule* rule, std::string_view property);
  void SetVariable(UI::StyleRule* rule, int sheet_idx, const std::string& property,
                   const std::string& value, bool coalesced = false);
  void RemoveVariable(UI::StyleRule* rule, int sheet_idx, size_t decl_index);
  bool AddVariable(UI::StyleRule* rule, int sheet_idx, const std::string& property);
  void ExtractInlineStylesToClass(UI::UxmlNode& node, const std::string& class_name);

  std::vector<MatchingRuleInfo> MatchingSelectors(UI::VisualElement* element) const;

  void SetAttribute(UI::UxmlNode& node, const std::string& name, const std::string& value,
                    const std::string& default_value, bool coalesced = false);
  void UnsetAttribute(UI::UxmlNode& node, const std::string& name);
  void SetClassList(UI::UxmlNode& node, const std::vector<std::string>& classes);
  void AddBinding(UI::UxmlNode& node);
  void RemoveBinding(UI::UxmlNode& node, size_t idx);
  void UpdateBinding(UI::UxmlNode& node, size_t idx, const UI::UxmlBindingDesc& binding,
                     bool coalesced = false);

  // ------------------------------------------------------------- Preview & Canvas
  void UpdatePreview(RenderHost* host, float dt);
  void DispatchPreviewInput(const UI::UIInputFrame& frame);
  UI::Panel* PreviewPanel() const { return panel_.get(); }

  glm::ivec2 CanvasSize() const { return canvas_size_; }
  void SetCanvasSize(glm::ivec2 size, bool fit = false);
  float Zoom() const { return zoom_; }
  void SetZoom(float zoom);
  glm::vec2 Pan() const { return pan_; }
  void SetPan(glm::vec2 pan);
  glm::vec4 Background() const { return background_; }
  void SetBackground(glm::vec4 bg);
  const std::string& Theme() const { return theme_; }
  void SetTheme(const std::string& theme);
  bool PreviewMode() const { return preview_mode_; }
  void SetPreviewMode(bool preview);

  void RequestFit() { fit_on_open_ = true; }
  bool FitIfRequested(glm::vec2 viewport_size);
  void FitToViewport(glm::vec2 viewport_size);
  void ResetZoom1To1();

  glm::vec2 ComputeCanvasOrigin(glm::vec2 viewport_size) const;
  UI::VisualElement* PickForEditing(glm::vec2 panel_point) const;

  Handle HitTestHandle(glm::vec2 local_mouse, glm::vec2 origin) const;
  void OnCanvasWheel(float wheel_delta, float wheel_h, bool ctrl_down);
  void OnCanvasMouseDown(glm::vec2 local_mouse, glm::vec2 origin, int button, bool alt_down);
  void OnCanvasMouseDrag(glm::vec2 local_mouse, glm::vec2 mouse_delta);
  void OnCanvasMouseUp();
  bool IsDraggingCanvas() const { return drag_ != Handle::kNone; }

  void UpdateHoveredElement(glm::vec2 local_mouse, glm::vec2 origin);
  void SetHoveredNode(const UI::UxmlNode* node);
  void ClearHoveredNode() { hovered_node_ = nullptr; }

  void RenderCanvasOverlay(OverlayCanvas& canvas, glm::vec2 origin) const;

  void DropNewTypeAtPoint(const std::string& type_name, glm::vec2 local_mouse, glm::vec2 origin);
  void DropTemplateAtPoint(const std::string& template_res_path, glm::vec2 local_mouse,
                           glm::vec2 origin);

 private:
  struct Snapshot {
    std::string uxml;
    std::vector<std::pair<std::string, std::string>> sheets;
    Selection selection;
  };

  Snapshot TakeSnapshot() const;
  void RestoreSnapshot(const Snapshot& snapshot);
  void MarkChanged(bool coalesced = false);
  void LoadSheets();
  void RebuildPreview();
  void SetInlineDeclarations(UI::UxmlNode& node,
                             const std::vector<UI::StyleDeclaration>& declarations);

  Console& console_;
  std::shared_ptr<Project> project_;

  std::shared_ptr<UI::VisualTreeAsset> doc_;
  std::string doc_path_;
  std::vector<SheetEntry> sheets_;
  int active_sheet_ = 0;
  bool dirty_ = false;
  Selection selection_;

  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  Snapshot last_committed_;
  bool edit_open_ = false;

  uint64_t doc_revision_ = 1;
  uint64_t selection_revision_ = 1;

  std::unique_ptr<UI::UIResources> resources_;
  std::unique_ptr<UI::Panel> panel_;
  UI::VisualElement* preview_root_ = nullptr;
  bool preview_dirty_ = true;
  bool preview_mode_ = false;
  glm::ivec2 canvas_size_{1280, 720};
  float zoom_ = 0.5f;
  bool fit_on_open_ = true;
  glm::vec2 pan_{0.0f, 0.0f};
  glm::vec4 background_{0.32f, 0.33f, 0.36f, 1.0f};
  std::string theme_;

  Handle drag_ = Handle::kNone;
  glm::vec2 drag_start_mouse_{0.0f, 0.0f};
  glm::vec2 drag_start_size_{0.0f, 0.0f};
  glm::vec2 drag_start_pos_{0.0f, 0.0f};
  const UI::UxmlNode* hovered_node_ = nullptr;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_UI_BUILDER_H
