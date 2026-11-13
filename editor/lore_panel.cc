#include "lore_panel.h"

#include <wx/menu.h>

#include <algorithm>
#include <filesystem>

#include "dialogs.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

wxString FormatChangeRow(const LoreVcs::Change& c) {
  std::string text = std::string("[") + c.code + "] " + c.path;
  if (!c.from.empty()) text += "  <- " + c.from;
  return wxString::FromUTF8(text);
}

bool ShowLoreOptionsModal(wxWindow* parent, LoreVcs& lore) {
  wxDialog dlg(parent, wxID_ANY, "Lore Options", wxDefaultPosition,
               wxSize(460, 220), wxDEFAULT_DIALOG_STYLE);

  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* remote_lbl = new wxStaticText(&dlg, wxID_ANY, "Repository Server URL");
  root->Add(remote_lbl, 0, wxLEFT | wxRIGHT | wxTOP, 12);

  auto* remote_ctrl = new wxTextCtrl(
      &dlg, wxID_ANY, wxString::FromUTF8(lore.ReadRemoteUrl()));
  remote_ctrl->SetHint("lore://host:port/repo");
  root->Add(remote_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

  auto* exe_lbl = new wxStaticText(&dlg, wxID_ANY, "lore Executable Path");
  root->Add(exe_lbl, 0, wxLEFT | wxRIGHT | wxTOP, 12);

  auto* exe_ctrl = new wxTextCtrl(
      &dlg, wxID_ANY,
      wxString::FromUTF8(lore.Settings().lore_executable));
  exe_ctrl->SetHint("lore");
  root->Add(exe_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  btns->AddStretchSpacer();
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  auto* ok = new wxButton(&dlg, wxID_OK, "Save");
  ok->SetDefault();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxALL, 12);

  dlg.SetSizer(root);
  dlg.CentreOnParent();
  if (dlg.ShowModal() != wxID_OK) return false;

  const std::string remote = remote_ctrl->GetValue().ToStdString();
  if (!remote.empty()) {
    lore.WriteRemoteUrl(remote);
  }
  lore.Settings().lore_executable = exe_ctrl->GetValue().ToStdString();
  lore.Settings().Save();
  lore.Refresh();
  return true;
}

}  // namespace

LorePanel::LorePanel(wxWindow* parent, LoreVcs& lore)
    : wxPanel(parent, wxID_ANY), lore_(lore) {
  root_sizer_ = new wxBoxSizer(wxVERTICAL);
  SetSizer(root_sizer_);
  RebuildUi();
}

void LorePanel::SyncFromController() {
  const bool has_proj = lore_.HasProject();
  const bool is_repo = lore_.IsRepository();
  const uint64_t rev = lore_.StateRevision();

  if (has_proj != last_has_project_ || is_repo != last_was_repo_) {
    RebuildUi();
    return;
  }
  if (rev != last_rev_) {
    last_rev_ = rev;
    PopulateRepoUi();
  }
}

void LorePanel::RebuildUi() {
  last_has_project_ = lore_.HasProject();
  last_was_repo_ = lore_.IsRepository();
  last_rev_ = lore_.StateRevision();

  Freeze();
  root_sizer_->Clear(true);
  init_panel_ = nullptr;
  repo_panel_ = nullptr;
  branch_choice_ = nullptr;
  staged_list_ = nullptr;
  unstaged_list_ = nullptr;
  history_list_ = nullptr;
  locks_list_ = nullptr;

  if (!last_has_project_) {
    auto* lbl =
        new wxStaticText(this, wxID_ANY, "Open a project to use Lore VCS.");
    lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    root_sizer_->Add(lbl, 0, wxALL, 14);
    Layout();
    Thaw();
    return;
  }

  if (!last_was_repo_) {
    init_panel_ = new wxPanel(this, wxID_ANY);
    auto* s = new wxBoxSizer(wxVERTICAL);

    auto* h = new wxStaticText(init_panel_, wxID_ANY,
                               "Initialize Lore Repository");
    h->SetFont(HeaderFont(1));
    s->Add(h, 0, wxBOTTOM, 6);

    auto* desc = new wxStaticText(
        init_panel_, wxID_ANY,
        "No .lore repository was found in this project folder.");
    desc->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    s->Add(desc, 0, wxBOTTOM, 12);

    auto* srv_lbl = new wxStaticText(init_panel_, wxID_ANY, "Server URL");
    s->Add(srv_lbl, 0, wxBOTTOM, 3);
    init_server_ = new wxTextCtrl(init_panel_, wxID_ANY, "http://127.0.0.1:4242");
    s->Add(init_server_, 0, wxEXPAND | wxBOTTOM, 8);

    auto* name_lbl =
        new wxStaticText(init_panel_, wxID_ANY, "Repository Name");
    s->Add(name_lbl, 0, wxBOTTOM, 3);
    std::string def_name =
        lore_.GetProject()
            ? lore_.GetProject()->GetRoot().filename().string()
            : "project";
    init_repo_ =
        new wxTextCtrl(init_panel_, wxID_ANY, wxString::FromUTF8(def_name));
    s->Add(init_repo_, 0, wxEXPAND | wxBOTTOM, 10);

    init_err_ = new wxStaticText(init_panel_, wxID_ANY, wxEmptyString);
    init_err_->SetFont(init_err_->GetFont().Bold());
    s->Add(init_err_, 0, wxBOTTOM, 8);

    auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
    auto* create_btn =
        new wxButton(init_panel_, wxID_ANY, "Create Repository");
    create_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      lore_.Initialize(init_server_->GetValue().ToStdString(),
                       init_repo_->GetValue().ToStdString());
    });
    auto* opt_btn = new wxButton(init_panel_, wxID_ANY, "Options...");
    opt_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
      ShowLoreOptionsModal(this, lore_);
    });
    btn_row->Add(create_btn, 0, wxRIGHT, 8);
    btn_row->Add(opt_btn, 0);
    s->Add(btn_row, 0);

    auto* wrap = new wxBoxSizer(wxVERTICAL);
    wrap->Add(s, 0, wxEXPAND | wxALL, 16);
    init_panel_->SetSizer(wrap);
    root_sizer_->Add(init_panel_, 1, wxEXPAND);
    Layout();
    Thaw();
    return;
  }

  // Full Repo UI
  repo_panel_ = new wxPanel(this, wxID_ANY);
  auto* rs = new wxBoxSizer(wxVERTICAL);

  // Header toolbar
  auto* bar = new wxPanel(repo_panel_, wxID_ANY);
  auto* bs = new wxBoxSizer(wxHORIZONTAL);

  auto* br_lbl = new wxStaticText(bar, wxID_ANY, "Branch:");
  bs->Add(br_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

  branch_choice_ = new wxChoice(bar, wxID_ANY);
  branch_choice_->SetMinSize(wxSize(130, -1));
  bs->Add(branch_choice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  sync_lbl_ = new wxStaticText(bar, wxID_ANY, wxEmptyString);
  bs->Add(sync_lbl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  busy_lbl_ = new wxStaticText(bar, wxID_ANY, wxEmptyString);
  busy_lbl_->SetFont(busy_lbl_->GetFont().Bold());
  bs->Add(busy_lbl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  auto* sync_btn = new wxButton(bar, wxID_ANY, "Sync", wxDefaultPosition,
                                wxDefaultSize, wxBU_EXACTFIT);
  push_btn_ = new wxButton(bar, wxID_ANY, "Push", wxDefaultPosition,
                           wxDefaultSize, wxBU_EXACTFIT);
  auto* refresh_btn = new wxButton(bar, wxID_ANY, "Refresh", wxDefaultPosition,
                                   wxDefaultSize, wxBU_EXACTFIT);
  auto* opt_btn = new wxButton(bar, wxID_ANY, "Options", wxDefaultPosition,
                               wxDefaultSize, wxBU_EXACTFIT);

  bs->Add(sync_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(push_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(refresh_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(opt_btn, 0, wxALIGN_CENTER_VERTICAL);

  auto* bwrap = new wxBoxSizer(wxVERTICAL);
  bwrap->Add(bs, 0, wxEXPAND | wxALL, 5);
  bar->SetSizer(bwrap);
  rs->Add(bar, 0, wxEXPAND);

  // Error banner
  err_banner_ = new wxPanel(repo_panel_, wxID_ANY, wxDefaultPosition,
                            wxDefaultSize, wxBORDER_SIMPLE);
  auto* es = new wxBoxSizer(wxHORIZONTAL);
  err_text_ = new wxStaticText(err_banner_, wxID_ANY, wxEmptyString);
  err_text_->SetFont(err_text_->GetFont().Bold());
  auto* dismiss_btn = new wxButton(err_banner_, wxID_ANY, "x", wxDefaultPosition,
                                   wxSize(22, 20), wxBU_EXACTFIT);
  es->Add(err_text_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  es->Add(dismiss_btn, 0, wxALIGN_CENTER_VERTICAL);
  auto* ewrap = new wxBoxSizer(wxVERTICAL);
  ewrap->Add(es, 0, wxEXPAND | wxALL, 5);
  err_banner_->SetSizer(ewrap);
  err_banner_->Hide();
  rs->Add(err_banner_, 0, wxEXPAND);

  // Tabs
  notebook_ = new wxNotebook(repo_panel_, wxID_ANY);

  // --- Tab 1: Changes ---
  auto* changes_tab = new wxPanel(notebook_, wxID_ANY);
  auto* cs = new wxBoxSizer(wxVERTICAL);

  auto* split = new wxSplitterWindow(changes_tab, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxSP_LIVE_UPDATE);
  split->SetMinimumPaneSize(120);

  auto* left_pane = new wxPanel(split, wxID_ANY);
  auto* ls = new wxBoxSizer(wxVERTICAL);

  auto* st_hdr = new wxBoxSizer(wxHORIZONTAL);
  auto* st_lbl = new wxStaticText(left_pane, wxID_ANY, "Staged Changes");
  auto* unstage_btn =
      new wxButton(left_pane, wxID_ANY, "Unstage", wxDefaultPosition,
                   wxDefaultSize, wxBU_EXACTFIT);
  st_hdr->Add(st_lbl, 1, wxALIGN_CENTER_VERTICAL);
  st_hdr->Add(unstage_btn, 0);
  ls->Add(st_hdr, 0, wxEXPAND | wxALL, 4);

  staged_list_ = new wxListCtrl(
      left_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxBORDER_NONE);
  staged_list_->InsertColumn(0, "File", wxLIST_FORMAT_LEFT, 260);
  ls->Add(staged_list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

  auto* un_hdr = new wxBoxSizer(wxHORIZONTAL);
  auto* un_lbl = new wxStaticText(left_pane, wxID_ANY, "Unstaged Changes");
  auto* stage_all_btn =
      new wxButton(left_pane, wxID_ANY, "Stage All", wxDefaultPosition,
                   wxDefaultSize, wxBU_EXACTFIT);
  un_hdr->Add(un_lbl, 1, wxALIGN_CENTER_VERTICAL);
  un_hdr->Add(stage_all_btn, 0);
  ls->Add(un_hdr, 0, wxEXPAND | wxALL, 4);

  unstaged_list_ = new wxListCtrl(
      left_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxBORDER_NONE);
  unstaged_list_->InsertColumn(0, "File", wxLIST_FORMAT_LEFT, 260);
  ls->Add(unstaged_list_, 2, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  left_pane->SetSizer(ls);

  auto* right_pane = new wxPanel(split, wxID_ANY);
  auto* rhs = new wxBoxSizer(wxVERTICAL);

  diff_view_ = new wxTextCtrl(
      right_pane, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL | wxBORDER_NONE);
  diff_view_->SetFont(MonospaceFont());
  rhs->Add(diff_view_, 1, wxEXPAND | wxALL, 4);

  commit_msg_ = new wxTextCtrl(right_pane, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxSize(-1, 56),
                               wxTE_MULTILINE);
  commit_msg_->SetHint("Commit message...");
  rhs->Add(commit_msg_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

  auto* cbtns = new wxBoxSizer(wxHORIZONTAL);
  commit_btn_ = new wxButton(right_pane, wxID_ANY, "Commit");
  commit_push_btn_ = new wxButton(right_pane, wxID_ANY, "Commit & Push");
  cbtns->Add(commit_btn_, 1, wxRIGHT, 6);
  cbtns->Add(commit_push_btn_, 1);
  rhs->Add(cbtns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  right_pane->SetSizer(rhs);

  split->SplitVertically(left_pane, right_pane, 260);
  cs->Add(split, 1, wxEXPAND);
  changes_tab->SetSizer(cs);
  notebook_->AddPage(changes_tab, "Changes");

  // --- Tab 2: History ---
  auto* hist_tab = new wxPanel(notebook_, wxID_ANY);
  auto* hsplit = new wxSplitterWindow(hist_tab, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, wxSP_LIVE_UPDATE);
  hsplit->SetMinimumPaneSize(110);

  auto* htop = new wxPanel(hsplit, wxID_ANY);
  auto* hts = new wxBoxSizer(wxVERTICAL);
  history_list_ = new wxListCtrl(
      htop, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  history_list_->InsertColumn(0, "Rev", wxLIST_FORMAT_LEFT, 56);
  history_list_->InsertColumn(1, "Author", wxLIST_FORMAT_LEFT, 110);
  history_list_->InsertColumn(2, "Date", wxLIST_FORMAT_LEFT, 120);
  history_list_->InsertColumn(3, "Message", wxLIST_FORMAT_LEFT, 300);
  hts->Add(history_list_, 1, wxEXPAND | wxALL, 4);

  auto* hactions = new wxBoxSizer(wxHORIZONTAL);
  auto* sync_rev_btn = new wxButton(htop, wxID_ANY, "Sync to Selected Rev");
  hactions->Add(sync_rev_btn, 0);
  hts->Add(hactions, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

  amend_bar_ = new wxPanel(htop, wxID_ANY);
  auto* as = new wxBoxSizer(wxHORIZONTAL);
  amend_msg_ = new wxTextCtrl(amend_bar_, wxID_ANY);
  amend_msg_->SetHint("New message for latest unpushed commit...");
  auto* amend_btn = new wxButton(amend_bar_, wxID_ANY, "Amend");
  as->Add(amend_msg_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  as->Add(amend_btn, 0, wxALIGN_CENTER_VERTICAL);
  amend_bar_->SetSizer(as);
  amend_bar_->Hide();
  hts->Add(amend_bar_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  htop->SetSizer(hts);

  rev_diff_view_ = new wxTextCtrl(
      hsplit, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL | wxBORDER_NONE);
  rev_diff_view_->SetFont(MonospaceFont());

  hsplit->SplitHorizontally(htop, rev_diff_view_, 190);
  auto* hist_wrap = new wxBoxSizer(wxVERTICAL);
  hist_wrap->Add(hsplit, 1, wxEXPAND);
  hist_tab->SetSizer(hist_wrap);
  notebook_->AddPage(hist_tab, "History");

  // --- Tab 3: Locks ---
  auto* locks_tab = new wxPanel(notebook_, wxID_ANY);
  auto* lks = new wxBoxSizer(wxVERTICAL);

  auto* lbar = new wxBoxSizer(wxHORIZONTAL);
  lock_path_ctrl_ = new wxTextCtrl(locks_tab, wxID_ANY);
  lock_path_ctrl_->SetHint("assets/maps/main.mmap");
  auto* lock_btn = new wxButton(locks_tab, wxID_ANY, "Lock File");
  auto* unlock_btn = new wxButton(locks_tab, wxID_ANY, "Release Selected");
  lbar->Add(lock_path_ctrl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  lbar->Add(lock_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  lbar->Add(unlock_btn, 0, wxALIGN_CENTER_VERTICAL);
  lks->Add(lbar, 0, wxEXPAND | wxALL, 6);

  locks_list_ = new wxListCtrl(
      locks_tab, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  locks_list_->InsertColumn(0, "Path", wxLIST_FORMAT_LEFT, 260);
  locks_list_->InsertColumn(1, "Owner", wxLIST_FORMAT_LEFT, 120);
  locks_list_->InsertColumn(2, "Branch", wxLIST_FORMAT_LEFT, 110);
  lks->Add(locks_list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  locks_tab->SetSizer(lks);
  notebook_->AddPage(locks_tab, "Locks (0)");

  rs->Add(notebook_, 1, wxEXPAND);
  repo_panel_->SetSizer(rs);
  root_sizer_->Add(repo_panel_, 1, wxEXPAND);

  // Event bindings
  branch_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    const int idx = branch_choice_->GetSelection();
    if (idx < 0 || idx >= static_cast<int>(branch_items_.size())) return;
    if (branch_is_new_[idx]) {
      if (auto name = PromptTextDialog(this, "Create Branch", "Branch name")) {
        lore_.CreateBranch(*name);
      } else {
        PopulateRepoUi();
      }
      return;
    }
    const std::string chosen = branch_items_[idx];
    if (chosen != lore_.Branch()) {
      lore_.SwitchBranch(chosen);
    }
  });

  sync_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { lore_.Sync(); });
  push_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { lore_.Push(); });
  refresh_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { lore_.Refresh(); });
  opt_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    ShowLoreOptionsModal(this, lore_);
  });
  dismiss_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    lore_.ClearError();
  });

  stage_all_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    lore_.StageAll();
  });
  unstage_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const long idx =
        staged_list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx >= 0 && idx < static_cast<long>(staged_items_.size())) {
      lore_.Unstage({staged_items_[idx].path});
    }
  });

  staged_list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(staged_items_.size())) {
      lore_.ShowDiff(staged_items_[idx].path);
    }
  });
  staged_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(staged_items_.size())) {
      lore_.Unstage({staged_items_[idx].path});
    }
  });
  staged_list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx < 0 || idx >= static_cast<long>(staged_items_.size())) return;
    const auto ch = staged_items_[idx];
    wxMenu menu;
    menu.Append(1, "Unstage");
    menu.Append(2, "Discard Changes");
    const bool locked = lore_.Locks().count(ch.path) > 0;
    menu.Append(3, locked ? "Unlock" : "Lock");
    menu.Bind(wxEVT_MENU, [this, ch, locked](wxCommandEvent& evt) {
      if (evt.GetId() == 1) lore_.Unstage({ch.path});
      else if (evt.GetId() == 2) lore_.Discard(ch);
      else if (evt.GetId() == 3) lore_.LockPaths({ch.path}, !locked);
    });
    staged_list_->PopupMenu(&menu);
  });

  unstaged_list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(unstaged_items_.size())) {
      lore_.ShowDiff(unstaged_items_[idx].path);
    }
  });
  unstaged_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(unstaged_items_.size())) {
      lore_.Stage({unstaged_items_[idx].path});
    }
  });
  unstaged_list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx < 0 || idx >= static_cast<long>(unstaged_items_.size())) return;
    const auto ch = unstaged_items_[idx];
    wxMenu menu;
    menu.Append(1, "Stage");
    menu.Append(2, "Discard Changes");
    const bool locked = lore_.Locks().count(ch.path) > 0;
    menu.Append(3, locked ? "Unlock" : "Lock");
    menu.Bind(wxEVT_MENU, [this, ch, locked](wxCommandEvent& evt) {
      if (evt.GetId() == 1) lore_.Stage({ch.path});
      else if (evt.GetId() == 2) lore_.Discard(ch);
      else if (evt.GetId() == 3) lore_.LockPaths({ch.path}, !locked);
    });
    unstaged_list_->PopupMenu(&menu);
  });

  commit_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string msg = commit_msg_->GetValue().ToStdString();
    if (!msg.empty()) {
      lore_.Commit(msg, false);
      commit_msg_->Clear();
    }
  });
  commit_push_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string msg = commit_msg_->GetValue().ToStdString();
    if (!msg.empty()) {
      lore_.Commit(msg, true);
      commit_msg_->Clear();
    }
  });

  history_list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
    const long idx = e.GetIndex();
    if (idx >= 0 && idx < static_cast<long>(history_items_.size())) {
      lore_.ShowRevision(history_items_[idx]);
    }
  });
  sync_rev_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const long idx =
        history_list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx >= 0 && idx < static_cast<long>(history_items_.size())) {
      lore_.Sync(std::to_string(history_items_[idx].number));
    }
  });
  amend_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string m = amend_msg_->GetValue().ToStdString();
    if (!m.empty()) {
      lore_.AmendMessage(m);
      amend_msg_->Clear();
    }
  });

  lock_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string p = lock_path_ctrl_->GetValue().ToStdString();
    if (!p.empty()) {
      lore_.LockPaths({p}, true);
      lock_path_ctrl_->Clear();
    }
  });
  unlock_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const long idx =
        locks_list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (idx >= 0 && idx < static_cast<long>(lock_keys_.size())) {
      lore_.LockPaths({lock_keys_[idx]}, false);
    }
  });

  PopulateRepoUi();
  Layout();
  Thaw();
}

void LorePanel::PopulateDiffText(wxTextCtrl* ctrl,
                                 const std::vector<std::string>& lines) {
  if (!ctrl) return;
  ctrl->Freeze();
  ctrl->Clear();
  const wxColour win_bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  const bool dark_theme =
      (win_bg.Red() * 299 + win_bg.Green() * 587 + win_bg.Blue() * 114) <
      128000;
  for (const auto& line : lines) {
    wxColour col = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0 ||
        line.rfind("diff ", 0) == 0 || line.rfind("Index:", 0) == 0) {
      col = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    } else if (!line.empty() && line[0] == '+') {
      col = dark_theme ? wxColour(106, 204, 117) : wxColour(30, 130, 45);
    } else if (!line.empty() && line[0] == '-') {
      col = dark_theme ? wxColour(235, 102, 102) : wxColour(185, 35, 35);
    } else if (line.rfind("@@", 0) == 0) {
      col = dark_theme ? wxColour(95, 165, 245) : wxColour(35, 95, 190);
    }
    ctrl->SetDefaultStyle(wxTextAttr(col));
    ctrl->AppendText(wxString::FromUTF8(line + "\n"));
  }
  ctrl->ShowPosition(0);
  ctrl->Thaw();
}

void LorePanel::PopulateRepoUi() {
  if (!last_was_repo_) {
    if (init_err_) {
      init_err_->SetLabel(wxString::FromUTF8(lore_.LastError()));
      init_panel_->Layout();
    }
    return;
  }

  // Branches dropdown
  if (branch_choice_) {
    branch_items_.clear();
    branch_is_new_.clear();
    wxArrayString labels;
    int sel = 0;

    auto add_branch = [&](const std::string& name, const std::string& label) {
      if (name == lore_.Branch()) sel = static_cast<int>(labels.size());
      branch_items_.push_back(name);
      branch_is_new_.push_back(false);
      labels.Add(wxString::FromUTF8(label));
    };

    if (lore_.Branches().empty() && !lore_.Branch().empty()) {
      add_branch(lore_.Branch(), lore_.Branch());
    }
    for (const auto& b : lore_.Branches()) add_branch(b, b);
    for (const auto& rb : lore_.RemoteBranches()) {
      if (std::find(lore_.Branches().begin(), lore_.Branches().end(), rb) ==
          lore_.Branches().end()) {
        add_branch(rb, rb + " (remote)");
      }
    }
    branch_items_.push_back("");
    branch_is_new_.push_back(true);
    labels.Add("<New Branch...>");

    branch_choice_->Set(labels);
    if (!labels.empty()) branch_choice_->SetSelection(sel);
  }

  // Status text
  if (sync_lbl_) {
    std::string status = "r" + std::to_string(std::max(0, lore_.LocalRevision()));
    if (!lore_.SyncState().empty()) status += " — " + lore_.SyncState();
    sync_lbl_->SetLabel(wxString::FromUTF8(status));
    sync_lbl_->SetFont(lore_.IsAheadOfRemote() ? GetFont().Bold() : GetFont());
  }
  if (busy_lbl_) {
    busy_lbl_->SetLabel(
        lore_.IsBusy()
            ? wxString::FromUTF8("Running " + lore_.CurrentOperation() + "...")
            : wxString());
  }
  if (push_btn_) {
    push_btn_->Enable(lore_.IsAheadOfRemote() && !lore_.IsBusy());
  }

  if (err_banner_ && err_text_) {
    if (lore_.LastError().empty()) {
      err_banner_->Hide();
    } else {
      err_text_->SetLabel(wxString::FromUTF8(lore_.LastError()));
      err_banner_->Show();
    }
  }

  // Changes lists
  staged_items_.clear();
  unstaged_items_.clear();
  for (const auto& c : lore_.Changes()) {
    if (c.section == LoreVcs::Section::kStaged) staged_items_.push_back(c);
    else unstaged_items_.push_back(c);
  }
  if (staged_list_) {
    staged_list_->Freeze();
    staged_list_->DeleteAllItems();
    for (long i = 0; i < static_cast<long>(staged_items_.size()); ++i) {
      staged_list_->InsertItem(i, FormatChangeRow(staged_items_[i]));
    }
    staged_list_->Thaw();
  }
  if (unstaged_list_) {
    unstaged_list_->Freeze();
    unstaged_list_->DeleteAllItems();
    for (long i = 0; i < static_cast<long>(unstaged_items_.size()); ++i) {
      unstaged_list_->InsertItem(i, FormatChangeRow(unstaged_items_[i]));
    }
    unstaged_list_->Thaw();
  }
  if (commit_btn_) commit_btn_->Enable(!staged_items_.empty());
  if (commit_push_btn_) commit_push_btn_->Enable(!staged_items_.empty());

  PopulateDiffText(diff_view_, lore_.DiffLines());

  // History list
  history_items_ = lore_.History();
  if (history_list_) {
    history_list_->Freeze();
    history_list_->DeleteAllItems();
    for (long i = 0; i < static_cast<long>(history_items_.size()); ++i) {
      const auto& rev = history_items_[i];
      history_list_->InsertItem(i, "r" + std::to_string(rev.number));
      history_list_->SetItem(i, 1, wxString::FromUTF8(rev.signature));
      history_list_->SetItem(i, 2, wxString::FromUTF8(rev.date));
      history_list_->SetItem(i, 3, wxString::FromUTF8(rev.message));
    }
    history_list_->Thaw();
  }
  if (amend_bar_) {
    amend_bar_->Show(lore_.HasUnpushedCommit());
  }
  PopulateDiffText(rev_diff_view_, lore_.RevisionLines());

  // Locks list
  lock_keys_.clear();
  if (locks_list_) {
    locks_list_->Freeze();
    locks_list_->DeleteAllItems();
    long row = 0;
    for (const auto& [path, info] : lore_.Locks()) {
      lock_keys_.push_back(path);
      locks_list_->InsertItem(row, wxString::FromUTF8(path));
      locks_list_->SetItem(row, 1, wxString::FromUTF8(info.owner));
      locks_list_->SetItem(row, 2, wxString::FromUTF8(info.branch));
      ++row;
    }
    locks_list_->Thaw();
  }
  if (notebook_) {
    notebook_->SetPageText(
        2, wxString::Format("Locks (%zu)", lore_.Locks().size()));
  }

  repo_panel_->Layout();
}

}  // namespace Mercury::EditorUI
