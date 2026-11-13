/*
 * input_actions_panel.h
 *
 * Retained-mode Input Actions (.minput) editor panel (Action Maps, Actions &
 * Bindings tree, Properties inspector with Pick menu and Listen capture, plus
 * AngelScript header codegen controls) and detachable InputActionsFrame.
 */
#ifndef MERCURY_EDITOR_INPUT_ACTIONS_PANEL_H
#define MERCURY_EDITOR_INPUT_ACTIONS_PANEL_H

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/scrolwin.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "editor_scene.h"

namespace Mercury::EditorUI {

class InputActionsPanel : public wxPanel {
 public:
  InputActionsPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController(float dt);
  void SetPoppedOut(bool popped_out);
  void SetOnPopout(std::function<void(bool)> cb) { on_popout_ = std::move(cb); }

 private:
  struct ListenTarget {
    enum class Kind { kNone, kBinding, kCompositePart };
    Kind kind = Kind::kNone;
    int action = -1;
    int index = -1;
    int part = -1;
    int part_idx = -1;
  };

  void BuildLayout();
  void RefreshFileChoices();
  void RefreshAll(bool rebuild_props = true);
  void RefreshTopBar();
  void RefreshMapsList();
  void RefreshActionsTree();
  void RefreshProperties();
  void SchedulePropsRefresh();

  void StartListen(ListenTarget target);
  void StopListen();
  void ApplyListenPath(const std::string& control_path);
  void ShowControlPickerMenu(wxWindow* anchor,
                             std::function<void(const std::string&)> on_pick);
  void OnCharHook(wxKeyEvent& evt);

  EditorScene& scene_;
  std::function<void(bool)> on_popout_;
  bool popped_out_ = false;

  uint64_t last_rev_ = 0;
  bool last_open_ = false;
  InputActionsWindow::Selection last_sel_;
  std::vector<std::string> minput_files_;
  bool syncing_ = false;
  bool editing_prop_text_ = false;
  bool props_refresh_pending_ = false;

  // Listen state
  ListenTarget listen_;
  bool listen_pad_[15] = {};

  // Top bar
  wxChoice* file_choice_ = nullptr;
  wxButton* new_file_btn_ = nullptr;
  wxButton* save_btn_ = nullptr;
  wxButton* undo_btn_ = nullptr;
  wxButton* redo_btn_ = nullptr;
  wxButton* project_actions_btn_ = nullptr;
  wxStaticText* project_actions_lbl_ = nullptr;
  wxButton* popout_btn_ = nullptr;

  wxPanel* codegen_bar_ = nullptr;
  wxCheckBox* gen_cb_ = nullptr;
  wxTextCtrl* class_edit_ = nullptr;
  wxTextCtrl* header_edit_ = nullptr;
  wxButton* write_header_btn_ = nullptr;
  wxStaticText* header_err_lbl_ = nullptr;

  wxPanel* empty_banner_ = nullptr;
  wxSplitterWindow* left_split_ = nullptr;
  wxSplitterWindow* right_split_ = nullptr;

  // Maps
  wxListBox* maps_list_ = nullptr;
  wxButton* add_map_btn_ = nullptr;
  wxButton* rename_map_btn_ = nullptr;
  wxButton* del_map_btn_ = nullptr;

  // Actions tree
  wxTreeCtrl* actions_tree_ = nullptr;
  wxButton* add_action_btn_ = nullptr;
  wxButton* add_binding_btn_ = nullptr;
  wxButton* add_comp_btn_ = nullptr;
  wxButton* dup_btn_ = nullptr;
  wxButton* del_btn_ = nullptr;

  // Properties
  wxScrolledWindow* props_scroll_ = nullptr;
  wxBoxSizer* props_sizer_ = nullptr;
  wxStaticText* dup_name_lbl_ = nullptr;
};

class InputActionsFrame : public wxFrame {
 public:
  InputActionsFrame(wxWindow* parent, EditorScene& scene,
                    std::function<void()> on_closed);

  InputActionsPanel* Panel() { return panel_; }
  void SyncFromController(float dt);
  void ClearOnClosed() { on_closed_ = nullptr; }

 private:
  InputActionsPanel* panel_ = nullptr;
  std::function<void()> on_closed_;
  bool last_dirty_ = false;
  std::string last_path_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_INPUT_ACTIONS_PANEL_H
