#include "launcher_frame.h"

#include <wx/dirdlg.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <wx/statline.h>

#include <algorithm>
#include <filesystem>

#include "dialogs.h"
#include "templates.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

class ProjectDropTarget : public wxFileDropTarget {
 public:
  explicit ProjectDropTarget(std::function<void(const std::string&)> on_drop)
      : on_drop_(std::move(on_drop)) {}

  bool OnDropFiles(wxCoord /*x*/, wxCoord /*y*/,
                   const wxArrayString& filenames) override {
    if (filenames.empty()) return false;
    on_drop_(filenames[0].ToStdString());
    return true;
  }

 private:
  std::function<void(const std::string&)> on_drop_;
};

std::string LastConsoleError(Console& console) {
  auto lines = console.Snapshot();
  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->level == Console::Level::kError) return it->text;
  }
  return {};
}

}  // namespace

LauncherFrame::LauncherFrame(EditorScene& scene,
                             std::function<void()> on_project_opened)
    : wxFrame(nullptr, wxID_ANY, "Mercury Engine — Project Launcher",
              wxDefaultPosition, wxSize(860, 560)),
      scene_(scene),
      on_project_opened_(std::move(on_project_opened)) {
  ApplyDarkPalette(this);
  BuildLayout();
  SetDropTarget(
      new ProjectDropTarget([this](const std::string& p) { TryOpenPath(p); }));
  RefreshRecentProjects();
  CentreOnScreen();
}

void LauncherFrame::BuildLayout() {
  auto* root = new wxBoxSizer(wxVERTICAL);

  // Header bar
  auto* hdr = new wxPanel(this);
  ApplyDarkPalette(hdr);
  hdr->SetBackgroundColour(Colors::kBgHeader);
  auto* hdr_s = new wxBoxSizer(wxHORIZONTAL);

  auto* title_box = new wxBoxSizer(wxVERTICAL);
  auto* title = new wxStaticText(hdr, wxID_ANY, "MERCURY ENGINE");
  title->SetFont(HeaderFont(14));
  title->SetForegroundColour(Colors::kTextPrimary);
  auto* sub = new wxStaticText(
      hdr, wxID_ANY, "Select a recent project, browse for a .mproj, or create one");
  sub->SetForegroundColour(Colors::kTextSecondary);
  title_box->Add(title, 0);
  title_box->Add(sub, 0, wxTOP, 2);

  auto* prefs_btn = new wxButton(hdr, wxID_ANY, "Preferences...",
                                 wxDefaultPosition, wxDefaultSize,
                                 wxBU_EXACTFIT);
  StyleButton(prefs_btn);
  prefs_btn->Bind(wxEVT_BUTTON,
                  [this](wxCommandEvent&) { ShowPreferencesDialog(this, scene_); });

  hdr_s->Add(title_box, 1, wxALIGN_CENTER_VERTICAL | wxALL, 12);
  hdr_s->Add(prefs_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
  hdr->SetSizer(hdr_s);
  root->Add(hdr, 0, wxEXPAND);
  root->Add(new wxStaticLine(this), 0, wxEXPAND);

  // Main two-column area
  auto* body = new wxBoxSizer(wxHORIZONTAL);

  // Left: Recent Projects
  auto* left = new wxPanel(this);
  ApplyDarkPalette(left);
  left->SetBackgroundColour(Colors::kBgCanvas);
  auto* left_s = new wxBoxSizer(wxVERTICAL);

  auto* rec_hdr = new wxBoxSizer(wxHORIZONTAL);
  auto* rec_lbl = new wxStaticText(left, wxID_ANY, "Recent Projects");
  rec_lbl->SetFont(HeaderFont(11));
  rec_lbl->SetForegroundColour(Colors::kTextPrimary);
  rec_hdr->Add(rec_lbl, 0, wxALIGN_CENTER_VERTICAL);
  rec_hdr->AddStretchSpacer();

  auto* open_file_btn = new wxButton(left, wxID_ANY, "Open .mproj...",
                                     wxDefaultPosition, wxDefaultSize,
                                     wxBU_EXACTFIT);
  auto* open_dir_btn = new wxButton(left, wxID_ANY, "Open Folder...",
                                    wxDefaultPosition, wxDefaultSize,
                                    wxBU_EXACTFIT);
  StyleButton(open_file_btn);
  StyleButton(open_dir_btn);
  rec_hdr->Add(open_file_btn, 0, wxRIGHT, 4);
  rec_hdr->Add(open_dir_btn, 0);
  left_s->Add(rec_hdr, 0, wxEXPAND | wxALL, 8);

  recent_list_ = new wxListCtrl(left, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_SINGLE_SEL);
  ApplyDarkPalette(recent_list_);
  recent_list_->SetBackgroundColour(Colors::kBgInput);
  recent_list_->InsertColumn(0, "Project", wxLIST_FORMAT_LEFT, 150);
  recent_list_->InsertColumn(1, "Location", wxLIST_FORMAT_LEFT, 310);
  left_s->Add(recent_list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto* rec_foot = new wxBoxSizer(wxHORIZONTAL);
  open_recent_btn_ = new wxButton(left, wxID_ANY, "Open Selected");
  remove_recent_btn_ = new wxButton(left, wxID_ANY, "Remove");
  StyleButton(open_recent_btn_, true);
  StyleButton(remove_recent_btn_);
  open_recent_btn_->Enable(false);
  remove_recent_btn_->Enable(false);
  rec_foot->Add(open_recent_btn_, 0, wxRIGHT, 6);
  rec_foot->Add(remove_recent_btn_, 0);
  left_s->Add(rec_foot, 0, wxALL, 8);
  left->SetSizer(left_s);

  open_file_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxFileDialog dlg(this, "Open Mercury Project", "", "",
                     "Mercury Project (*.mproj)|*.mproj|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
      TryOpenPath(dlg.GetPath().ToStdString());
    }
  });
  open_dir_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxDirDialog dlg(this, "Open Mercury Project Folder", "",
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
      TryOpenPath(dlg.GetPath().ToStdString());
    }
  });

  recent_list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
    long idx = recent_list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
    const bool valid = idx >= 0;
    open_recent_btn_->Enable(valid);
    remove_recent_btn_->Enable(valid);
  });
  recent_list_->Bind(wxEVT_LIST_ITEM_DESELECTED, [this](wxListEvent&) {
    open_recent_btn_->Enable(false);
    remove_recent_btn_->Enable(false);
  });
  recent_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& evt) {
    long idx = evt.GetIndex();
    const auto& rec = scene_.Settings().recent_projects;
    if (idx >= 0 && static_cast<size_t>(idx) < rec.size()) {
      TryOpenPath(rec[idx]);
    }
  });
  recent_list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& evt) {
    long idx = evt.GetIndex();
    auto& rec = scene_.Settings().recent_projects;
    if (idx < 0 || static_cast<size_t>(idx) >= rec.size()) return;
    wxMenu menu;
    menu.Append(1, "Open Project");
    menu.Append(2, "Remove from Recent");
    menu.Bind(wxEVT_MENU, [this, idx](wxCommandEvent& m) {
      auto& r = scene_.Settings().recent_projects;
      if (idx < 0 || static_cast<size_t>(idx) >= r.size()) return;
      if (m.GetId() == 1) {
        TryOpenPath(r[idx]);
      } else if (m.GetId() == 2) {
        r.erase(r.begin() + idx);
        scene_.Settings().Save();
        RefreshRecentProjects();
      }
    });
    PopupMenu(&menu);
  });

  open_recent_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long idx = recent_list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
    const auto& rec = scene_.Settings().recent_projects;
    if (idx >= 0 && static_cast<size_t>(idx) < rec.size()) {
      TryOpenPath(rec[idx]);
    }
  });
  remove_recent_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    long idx = recent_list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                         wxLIST_STATE_SELECTED);
    auto& rec = scene_.Settings().recent_projects;
    if (idx >= 0 && static_cast<size_t>(idx) < rec.size()) {
      rec.erase(rec.begin() + idx);
      scene_.Settings().Save();
      RefreshRecentProjects();
    }
  });

  // Right: New Project card
  auto* right = new wxPanel(this);
  ApplyDarkPalette(right);
  right->SetBackgroundColour(Colors::kBgPanel);
  auto* right_s = new wxBoxSizer(wxVERTICAL);

  auto* new_lbl = new wxStaticText(right, wxID_ANY, "New Project");
  new_lbl->SetFont(HeaderFont(11));
  new_lbl->SetForegroundColour(Colors::kTextPrimary);
  right_s->Add(new_lbl, 0, wxALL, 8);
  right_s->Add(new wxStaticLine(right), 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto* nl = new wxStaticText(right, wxID_ANY, "Project Name");
  nl->SetForegroundColour(Colors::kTextSecondary);
  new_name_ctrl_ = new wxTextCtrl(right, wxID_ANY, "MyGame");
  StyleTextCtrl(new_name_ctrl_);
  right_s->Add(nl, 0, wxLEFT | wxRIGHT | wxTOP, 10);
  right_s->Add(new_name_ctrl_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

  auto* dim_lbl = new wxStaticText(right, wxID_ANY, "Template Mode");
  dim_lbl->SetForegroundColour(Colors::kTextSecondary);
  right_s->Add(dim_lbl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

  auto* dim_row = new wxBoxSizer(wxHORIZONTAL);
  radio_3d_ =
      new wxRadioButton(right, wxID_ANY, "3D", wxDefaultPosition, wxDefaultSize,
                        wxRB_GROUP);
  radio_2d_ = new wxRadioButton(right, wxID_ANY, "2D");
  radio_3d_->SetForegroundColour(Colors::kTextPrimary);
  radio_2d_->SetForegroundColour(Colors::kTextPrimary);
  radio_3d_->SetValue(true);
  dim_row->Add(radio_3d_, 0, wxRIGHT, 16);
  dim_row->Add(radio_2d_, 0);
  right_s->Add(dim_row, 0, wxLEFT | wxRIGHT | wxTOP, 6);

  auto* fl = new wxStaticText(right, wxID_ANY, "Parent Folder");
  fl->SetForegroundColour(Colors::kTextSecondary);
  right_s->Add(fl, 0, wxLEFT | wxRIGHT | wxTOP, 10);

  auto* f_row = new wxBoxSizer(wxHORIZONTAL);
  new_folder_ctrl_ = new wxTextCtrl(
      right, wxID_ANY, wxString::FromUTF8(EditorSettings::HomeDir().string()));
  StyleTextCtrl(new_folder_ctrl_);
  auto* browse_btn = new wxButton(right, wxID_ANY, "Browse...",
                                  wxDefaultPosition, wxDefaultSize,
                                  wxBU_EXACTFIT);
  StyleButton(browse_btn);
  browse_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    wxDirDialog dlg(this, "Choose Parent Folder",
                    new_folder_ctrl_->GetValue());
    if (dlg.ShowModal() == wxID_OK) {
      new_folder_ctrl_->SetValue(dlg.GetPath());
    }
  });
  f_row->Add(new_folder_ctrl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  f_row->Add(browse_btn, 0, wxALIGN_CENTER_VERTICAL);
  right_s->Add(f_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

  auto* create_btn = new wxButton(right, wxID_ANY, "Create Project");
  StyleButton(create_btn, true);
  create_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string name = new_name_ctrl_->GetValue().ToStdString();
    const std::string folder = new_folder_ctrl_->GetValue().ToStdString();
    const bool is_3d = radio_3d_->GetValue();
    std::string err;
    fs::path mproj = Templates::CreateProject(folder, name, is_3d, &err);
    if (mproj.empty()) {
      SetError(err.empty() ? "Failed to create project." : err);
      return;
    }
    TryOpenPath(mproj.string());
  });
  right_s->AddStretchSpacer();
  right_s->Add(create_btn, 0, wxEXPAND | wxALL, 10);
  right->SetSizer(right_s);

  body->Add(left, 3, wxEXPAND);
  body->Add(new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxLI_VERTICAL),
            0, wxEXPAND);
  body->Add(right, 2, wxEXPAND);
  root->Add(body, 1, wxEXPAND);

  // Footer error / DnD hint
  root->Add(new wxStaticLine(this), 0, wxEXPAND);
  auto* foot = new wxPanel(this);
  ApplyDarkPalette(foot);
  foot->SetBackgroundColour(Colors::kBgCanvas);
  auto* foot_s = new wxBoxSizer(wxHORIZONTAL);
  error_lbl_ = new wxStaticText(
      foot, wxID_ANY, "Tip: Drag and drop a .mproj file or project folder here.");
  error_lbl_->SetForegroundColour(Colors::kTextSecondary);
  foot_s->Add(error_lbl_, 1, wxALIGN_CENTER_VERTICAL | wxALL, 8);
  foot->SetSizer(foot_s);
  root->Add(foot, 0, wxEXPAND);

  SetSizer(root);
}

void LauncherFrame::RefreshRecentProjects() {
  recent_list_->DeleteAllItems();
  const auto& rec = scene_.Settings().recent_projects;
  for (size_t i = 0; i < rec.size(); ++i) {
    fs::path p(rec[i]);
    std::error_code ec;
    const bool exists = fs::exists(p, ec);
    std::string name = p.stem().string();
    if (!exists) name += " (missing)";
    long row = recent_list_->InsertItem(static_cast<long>(i),
                                        wxString::FromUTF8(name));
    recent_list_->SetItem(row, 1, wxString::FromUTF8(rec[i]));
    if (!exists) {
      recent_list_->SetItemTextColour(row, Colors::kTextMuted);
    } else {
      recent_list_->SetItemTextColour(row, Colors::kTextPrimary);
    }
  }
  open_recent_btn_->Enable(false);
  remove_recent_btn_->Enable(false);
}

void LauncherFrame::SetError(const std::string& msg) {
  if (msg.empty()) {
    error_lbl_->SetLabel(
        "Tip: Drag and drop a .mproj file or project folder here.");
    error_lbl_->SetForegroundColour(Colors::kTextSecondary);
  } else {
    error_lbl_->SetLabel(wxString::FromUTF8(msg));
    error_lbl_->SetForegroundColour(Colors::kAccentRed);
  }
  Layout();
}

void LauncherFrame::TryOpenPath(const std::string& path_str) {
  SetError("");
  const size_t before_err_check = scene_.ConsoleRef().GetVersion();
  if (scene_.OpenProjectOrDirectory(fs::path(path_str))) {
    if (on_project_opened_) {
      CallAfter([cb = on_project_opened_]() { cb(); });
    }
    return;
  }
  (void)before_err_check;
  std::string err = LastConsoleError(scene_.ConsoleRef());
  if (err.empty()) err = "Could not open project: " + path_str;
  SetError(err);
  RefreshRecentProjects();
}

}  // namespace Mercury::EditorUI
