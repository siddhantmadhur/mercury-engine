/*
 * ui_builder_panel.h
 *
 * Retained-mode UI Builder panel (StyleSheets, Hierarchy, Library, Live
 * Vulkan UI Preview Canvas, Code tabs, and Style/Attribute Inspector) plus
 * detachable native UIBuilderFrame window.
 */
#ifndef MERCURY_EDITOR_UI_BUILDER_PANEL_H
#define MERCURY_EDITOR_UI_BUILDER_PANEL_H

#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "editor_scene.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

class UIBuilderPanel : public wxPanel {
 public:
  UIBuilderPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController(float dt);
  void SetPoppedOut(bool popped_out);
  void SetOnPopout(std::function<void(bool)> cb) { on_popout_ = std::move(cb); }

 private:
  void BuildLayout();
  void RefreshAll();
  void RefreshTopBar();
  void RefreshSheetsTree();
  void RefreshHierarchyTree();
  void RefreshLibrary();
  void RefreshCodeTabs();
  void RefreshInspector();
  void ScheduleInspectorRefresh();
  void UpdateCanvasInputPreview(float dt);

  // wxKeyEvent / Mouse -> UIInputEvent helpers for Preview mode
  void QueuePreviewKey(wxKeyEvent& evt, bool down);

  EditorScene& scene_;
  std::function<void(bool)> on_popout_;
  bool popped_out_ = false;

  uint64_t last_doc_rev_ = 0;
  uint64_t last_sel_rev_ = 0;
  bool last_open_ = false;
  bool last_preview_mode_ = false;
  bool dragging_ = false;
  bool inspector_rebuild_pending_ = false;

  // Top bar widgets
  wxStaticText* file_lbl_ = nullptr;
  wxButton* save_btn_ = nullptr;
  wxButton* revert_btn_ = nullptr;
  wxButton* close_btn_ = nullptr;
  wxButton* undo_btn_ = nullptr;
  wxButton* redo_btn_ = nullptr;
  wxButton* dup_btn_ = nullptr;
  wxButton* del_btn_ = nullptr;
  wxCheckBox* preview_cb_ = nullptr;
  wxChoice* preset_choice_ = nullptr;
  DragNumberCtrl* canvas_w_ = nullptr;
  DragNumberCtrl* canvas_h_ = nullptr;
  ColorSwatchCtrl* bg_swatch_ = nullptr;
  wxChoice* theme_choice_ = nullptr;
  std::vector<std::string> theme_paths_;
  wxButton* popout_btn_ = nullptr;

  wxPanel* empty_banner_ = nullptr;
  wxSplitterWindow* main_split_ = nullptr;

  // Left: StyleSheets + Hierarchy + Library
  wxTextCtrl* new_selector_ = nullptr;
  wxTreeCtrl* sheets_tree_ = nullptr;
  wxTreeCtrl* hier_tree_ = nullptr;
  wxTextCtrl* lib_search_ = nullptr;
  wxTreeCtrl* lib_tree_ = nullptr;
  bool syncing_trees_ = false;

  // Center: Canvas + Code
  wxPanel* canvas_ = nullptr;
  wxFont overlay_font_;
  std::vector<uint8_t> ui_pixels_;
  wxBitmap ui_bmp_;
  uint64_t ui_frame_id_ = 0;
  bool canvas_hovered_ = false;
  bool canvas_dragging_ = false;
  glm::vec2 local_mouse_{0.0f, 0.0f};
  glm::vec2 prev_mouse_{0.0f, 0.0f};
  bool mouse_valid_ = false;
  std::vector<UI::UIInputEvent> pending_preview_events_;

  wxNotebook* code_tabs_ = nullptr;

  // Right: Inspector
  wxScrolledWindow* inspector_scroll_ = nullptr;
  wxBoxSizer* inspector_sizer_ = nullptr;
  std::set<std::string> collapsed_groups_;
};

class UIBuilderFrame : public wxFrame {
 public:
  UIBuilderFrame(wxWindow* parent, EditorScene& scene,
                 std::function<void()> on_closed);

  UIBuilderPanel* Panel() { return panel_; }
  void SyncFromController(float dt);

 private:
  UIBuilderPanel* panel_ = nullptr;
  std::function<void()> on_closed_;
  bool last_dirty_ = false;
  std::string last_path_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_UI_BUILDER_PANEL_H
