/*
 * lore_panel.h
 *
 * Dockable Lore VCS panel with repository initialization, native branch
 * dropdown, Changes (staged/unstaged/diff/commit), History (sync/amend), and
 * Locks management tabs.
 */
#ifndef MERCURY_EDITOR_LORE_PANEL_H
#define MERCURY_EDITOR_LORE_PANEL_H

#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/splitter.h>
#include <wx/wx.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lore_vcs.h"

namespace Mercury::EditorUI {

class LorePanel : public wxPanel {
 public:
  LorePanel(wxWindow* parent, LoreVcs& lore);

  void SyncFromController();

 private:
  void RebuildUi();
  void PopulateRepoUi();
  void ShowLoreOptionsDialog();
  void PopulateDiffText(wxTextCtrl* ctrl,
                        const std::vector<std::string>& lines);

  LoreVcs& lore_;
  uint64_t last_rev_ = 0;
  bool last_was_repo_ = false;
  bool last_has_project_ = false;

  wxBoxSizer* root_sizer_ = nullptr;

  // Empty / Init state widgets
  wxPanel* init_panel_ = nullptr;
  wxTextCtrl* init_server_ = nullptr;
  wxTextCtrl* init_repo_ = nullptr;
  wxStaticText* init_err_ = nullptr;

  // Repo state widgets
  wxPanel* repo_panel_ = nullptr;
  wxChoice* branch_choice_ = nullptr;
  std::vector<std::string> branch_items_;
  std::vector<bool> branch_is_new_;
  wxStaticText* sync_lbl_ = nullptr;
  wxStaticText* busy_lbl_ = nullptr;
  wxPanel* err_banner_ = nullptr;
  wxStaticText* err_text_ = nullptr;
  wxButton* push_btn_ = nullptr;

  wxNotebook* notebook_ = nullptr;

  // Changes tab
  wxListCtrl* staged_list_ = nullptr;
  wxListCtrl* unstaged_list_ = nullptr;
  std::vector<LoreVcs::Change> staged_items_;
  std::vector<LoreVcs::Change> unstaged_items_;
  wxTextCtrl* diff_view_ = nullptr;
  wxTextCtrl* commit_msg_ = nullptr;
  wxButton* commit_btn_ = nullptr;
  wxButton* commit_push_btn_ = nullptr;

  // History tab
  wxListCtrl* history_list_ = nullptr;
  wxTextCtrl* amend_msg_ = nullptr;
  wxPanel* amend_bar_ = nullptr;
  wxTextCtrl* rev_diff_view_ = nullptr;
  std::vector<LoreVcs::Revision> history_items_;

  // Locks tab
  wxTextCtrl* lock_path_ctrl_ = nullptr;
  wxListCtrl* locks_list_ = nullptr;
  std::vector<std::string> lock_keys_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_LORE_PANEL_H
