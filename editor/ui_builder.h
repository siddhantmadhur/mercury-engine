/*
 * ui_builder.h
 *
 * The UI Builder window, modeled on Unity's UI Builder: edit a .uxml
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
 *
 * The editor itself uses Dear ImGui; only the canvas is drawn with the game's
 * UI Toolkit renderer, so what you see is what the game shows.
 */
#ifndef MERCURY_EDITOR_UI_BUILDER_H
#define MERCURY_EDITOR_UI_BUILDER_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "imgui.h"
#include "project/project.h"
#include "scene/scene.h"
#include "ui/panel.h"
#include "ui/uxml.h"

namespace Mercury {

class UIBuilder {
 public:
  static constexpr const char* kWindowName = "UI Builder";

  explicit UIBuilder(Console& console);
  ~UIBuilder();

  void SetProject(std::shared_ptr<Project> project);

  // Opens a .uxml document (res:// path); false if it can't be read.
  bool Open(const std::string& res_path);
  void Close();
  bool IsOpen() const { return doc_ != nullptr; }
  const std::string& path() const { return doc_path_; }
  bool IsDirty() const { return dirty_; }
  // Writes the document and every changed style sheet.
  bool Save();

  void Undo();
  void Redo();
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  // True while the builder window (or one of its panes) has focus, so
  // Ctrl+S / Ctrl+Z go to it instead of the map.
  bool HasFocus() const { return has_focus_; }

  // Draws the window; `open` is cleared when it is closed.
  void Draw(RenderHost* host, bool* open);
  // Lists .uxml/.uss/.png... files in the project, for pickers.
  std::function<std::vector<std::string>(const std::vector<std::string>&)> list_files;

 private:
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
  struct Snapshot {
    std::string uxml;
    std::vector<std::pair<std::string, std::string>> sheets;  // src, text
    Selection selection;
  };

  // ------------------------------------------------------------ document
  Snapshot TakeSnapshot() const;
  void RestoreSnapshot(const Snapshot& snapshot);
  void MarkChanged();
  void EndFrameEdits();
  void LoadSheets();
  int AddSheet(const std::string& src);
  std::string RelativeToDocument(const std::string& res_path) const;
  UI::UxmlNode* NodeAt(const std::vector<int>& path);
  std::optional<std::vector<int>> PathOf(const UI::UxmlNode* node) const;
  UI::UxmlNode* SelectedNode();
  UI::StyleRule* SelectedRule();
  // The element in the preview made from `node`.
  UI::VisualElement* ElementFor(const UI::UxmlNode* node) const;
  UI::UxmlNode NewNode(const std::string& type) const;
  void InsertNode(const std::vector<int>& parent, int index, UI::UxmlNode node);
  std::optional<UI::UxmlNode> RemoveNode(const std::vector<int>& path);
  void MoveNode(const std::vector<int>& from, const std::vector<int>& parent, int index);
  void DeleteSelected();
  void DuplicateSelected();

  // ------------------------------------------------------------- preview
  void RebuildPreview();
  void UpdatePreview(RenderHost* host);
  UI::VisualElement* PickForEditing(glm::vec2 panel_point) const;
  void ForwardInput(ImVec2 canvas_origin);

  // --------------------------------------------------------------- panes
  void DrawToolbar();
  void DrawStyleSheets();
  void DrawHierarchy();
  void DrawHierarchyNode(UI::UxmlNode& node, std::vector<int>& path);
  void DrawLibrary();
  void DrawViewport(RenderHost* host);
  void DrawCanvasOverlay(ImDrawList* draw, ImVec2 origin);
  void HandleCanvasMouse(ImVec2 origin, bool hovered, bool active);
  void DrawInspector();
  void DrawDocumentSettings();
  void DrawAttributes(UI::UxmlNode& node);
  void DrawClassList(UI::UxmlNode& node);
  void DrawBindings(UI::UxmlNode& node);
  void DrawMatchingSelectors(UI::VisualElement* element);
  void DrawStyleProperties(UI::UxmlNode* node, UI::StyleRule* rule);
  void DrawCode();

  // Inline style of a node as declarations.
  std::vector<UI::StyleDeclaration> InlineDeclarations(const UI::UxmlNode& node) const;
  void SetInlineDeclarations(UI::UxmlNode& node,
                             const std::vector<UI::StyleDeclaration>& declarations);
  void SetStyle(UI::UxmlNode* node, UI::StyleRule* rule, std::string_view property,
                std::string_view value);
  void UnsetStyle(UI::UxmlNode* node, UI::StyleRule* rule, std::string_view property);
  void ExtractInlineStylesToClass(UI::UxmlNode& node, const std::string& class_name);

  Console& console_;
  std::shared_ptr<Project> project_;
  bool has_focus_ = false;

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

  // Preview: a panel of its own, rebuilt from the document after edits.
  std::unique_ptr<UI::UIResources> resources_;
  std::unique_ptr<UI::Panel> panel_;
  UI::VisualElement* preview_root_ = nullptr;
  bool preview_dirty_ = true;
  bool preview_mode_ = false;
  bool preview_keyboard_ = false;
  glm::ivec2 canvas_size_{1280, 720};
  float zoom_ = 0.5f;
  bool fit_on_open_ = true;
  ImVec2 pan_{0, 0};
  ImVec2 viewport_size_{1, 1};
  glm::vec4 background_{0.32f, 0.33f, 0.36f, 1.0f};
  std::string theme_;  // "" uses the default runtime theme

  // Canvas drags.
  enum class Handle { kNone, kMove, kRight, kBottom, kCorner, kPan };
  Handle drag_ = Handle::kNone;
  ImVec2 drag_start_mouse_{0, 0};
  glm::vec2 drag_start_size_{0.0f};
  glm::vec2 drag_start_pos_{0.0f};
  const UI::UxmlNode* hovered_node_ = nullptr;
  // A structural edit from the Hierarchy, applied once the tree is drawn.
  std::function<void()> pending_edit_;

  char new_selector_[256] = "";
  char new_class_[128] = "";
  char extract_class_[128] = "";
  int code_tab_ = 0;
  float left_width_ = 260.0f;
  float right_width_ = 330.0f;
  float code_height_ = 160.0f;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_UI_BUILDER_H
