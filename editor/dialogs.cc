#include "dialogs.h"

#include <wx/choice.h>
#include <wx/dirdlg.h>
#include <wx/listctrl.h>
#include <wx/statline.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include "editor_icons.h"
#include "scripting/type_registry.h"
#include "templates.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

std::optional<std::string> PromptTextDialog(wxWindow* parent,
                                            const wxString& title,
                                            const wxString& label,
                                            const wxString& initial_value,
                                            const wxString& confirm_label) {
  wxDialog dlg(parent, wxID_ANY, title, wxDefaultPosition, wxSize(420, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* lbl = new wxStaticText(&dlg, wxID_ANY, label);
  root->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 14);

  auto* input = new wxTextCtrl(&dlg, wxID_ANY, initial_value, wxDefaultPosition,
                               wxDefaultSize, wxTE_PROCESS_ENTER);
  root->Add(input, 0, wxEXPAND | wxALL, 14);
  input->SetSelection(-1, -1);
  input->SetFocus();

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, confirm_label);
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  input->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
    if (!input->GetValue().Strip(wxString::both).IsEmpty()) {
      dlg.EndModal(wxID_OK);
    }
  });

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(420, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return std::nullopt;
  wxString val = input->GetValue().Strip(wxString::both);
  if (val.IsEmpty()) return std::nullopt;
  return val.ToStdString();
}

std::optional<std::string> ShowNewScriptDialog(wxWindow* parent) {
  wxDialog dlg(parent, wxID_ANY, "New Script", wxDefaultPosition,
               wxSize(440, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* lbl = new wxStaticText(&dlg, wxID_ANY, "Class Name (AngelScript):");
  root->Add(lbl, 0, wxLEFT | wxRIGHT | wxTOP, 14);

  auto* input = new wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_PROCESS_ENTER);
  root->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);

  auto* preview =
      new wxStaticText(&dlg, wxID_ANY, "Enter a valid C++/AngelScript class name");
  preview->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  root->Add(preview, 0, wxLEFT | wxRIGHT | wxTOP, 8);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, "Create");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->Enable(false);
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxALL, 14);

  auto update = [&]() {
    const std::string name = input->GetValue().ToStdString();
    const bool valid = Templates::IsValidIdentifier(name);
    ok->Enable(valid);
    if (name.empty()) {
      preview->SetLabel("Enter a valid C++/AngelScript class name");
      preview->SetForegroundColour(
          wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      preview->SetFont(dlg.GetFont());
    } else if (valid) {
      preview->SetLabel(wxString::FromUTF8(
          "Will create: scripts/" + Templates::ToSnakeCase(name) + ".as"));
      preview->SetForegroundColour(wxNullColour);
      preview->SetFont(dlg.GetFont());
    } else {
      preview->SetLabel("Invalid identifier (letters, digits, underscores; cannot start with digit)");
      preview->SetForegroundColour(wxNullColour);
      preview->SetFont(dlg.GetFont().Bold());
    }
  };
  input->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { update(); });
  input->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
    if (Templates::IsValidIdentifier(input->GetValue().ToStdString())) {
      dlg.EndModal(wxID_OK);
    }
  });
  input->SetFocus();

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(440, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return std::nullopt;
  return input->GetValue().ToStdString();
}

bool ShowReleaseBuildDialog(wxWindow* parent, EditorScene& scene) {
  auto project = scene.GetProject();
  if (!project) return false;

  wxDialog dlg(parent, wxID_ANY, "Build Game (Release)", wxDefaultPosition,
               wxSize(520, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* desc = new wxStaticText(
      &dlg, wxID_ANY,
      "Creates a shippable folder containing the Release player executable and "
      "packed .mpack archives.");
  desc->Wrap(480);
  root->Add(desc, 0, wxLEFT | wxRIGHT | wxTOP, 14);

  auto* grid = new wxFlexGridSizer(2, 10, 10);
  grid->AddGrowableCol(1, 1);

  auto* out_lbl = new wxStaticText(&dlg, wxID_ANY, "Output Folder:");
  auto* out_row = new wxBoxSizer(wxHORIZONTAL);
  const std::string default_out =
      project->GetBuildOutput().empty()
          ? ReleaseBuild::DefaultOutput(*project)
          : project->GetBuildOutput();
  auto* out_ctrl = new wxTextCtrl(&dlg, wxID_ANY, wxString::FromUTF8(default_out));
  auto* browse = new wxButton(&dlg, wxID_ANY, "Browse...");
  out_row->Add(out_ctrl, 1, wxEXPAND | wxRIGHT, 6);
  out_row->Add(browse, 0);

  auto* pack_lbl = new wxStaticText(&dlg, wxID_ANY, "Pack Split Size:");
  wxArrayString pack_choices;
  int pack_sel = 0;
  for (size_t i = 0; i < EditorScene::kNumPackSizes; ++i) {
    pack_choices.Add(EditorScene::kPackSizes[i].label);
    if (EditorScene::kPackSizes[i].megabytes == scene.Settings().pack_size_mb) {
      pack_sel = static_cast<int>(i);
    }
  }
  auto* pack_choice = new wxChoice(&dlg, wxID_ANY, wxDefaultPosition,
                                   wxDefaultSize, pack_choices);
  pack_choice->SetSelection(pack_sel);

  grid->Add(out_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(out_row, 1, wxEXPAND);
  grid->Add(pack_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(pack_choice, 1, wxEXPAND);
  root->Add(grid, 0, wxEXPAND | wxALL, 14);

  browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    fs::path initial = project->Resolve(out_ctrl->GetValue().ToStdString());
    if (!fs::exists(initial)) initial = project->GetRoot();
    wxDirDialog dir_dlg(&dlg, "Select Build Output Folder",
                        wxString::FromUTF8(initial.string()),
                        wxDD_DEFAULT_STYLE);
    if (dir_dlg.ShowModal() == wxID_OK) {
      out_ctrl->SetValue(
          wxString::FromUTF8(project->ToResPath(dir_dlg.GetPath().ToStdString())));
    }
  });

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, "Build Release");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(520, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return false;

  const int idx = pack_choice->GetSelection();
  if (idx >= 0 && static_cast<size_t>(idx) < EditorScene::kNumPackSizes) {
    scene.Settings().pack_size_mb = EditorScene::kPackSizes[idx].megabytes;
    scene.Settings().Save();
  }
  return scene.BuildGame(out_ctrl->GetValue().ToStdString());
}

bool ShowPreferencesDialog(wxWindow* parent, EditorScene& scene) {
  wxDialog dlg(parent, wxID_ANY, "Editor Settings", wxDefaultPosition,
               wxSize(560, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* grid = new wxFlexGridSizer(2, 10, 12);
  grid->AddGrowableCol(1, 1);

  auto* ed_lbl = new wxStaticText(&dlg, wxID_ANY, "External Script Editor:");
  auto* ed_ctrl = new wxTextCtrl(
      &dlg, wxID_ANY, wxString::FromUTF8(scene.Settings().external_editor));
  ed_ctrl->SetToolTip(
      "Command template with {project} and {file} placeholders. Leave empty "
      "for system default app.");

  auto* preset_lbl = new wxStaticText(&dlg, wxID_ANY, "Presets:");
  auto* preset_row = new wxBoxSizer(wxHORIZONTAL);
  auto add_preset = [&](const char* name, const char* cmd) {
    auto* btn = new wxButton(&dlg, wxID_ANY, name);
    ApplyButtonPadding(btn, 8);
    btn->Bind(wxEVT_BUTTON, [ed_ctrl, cmd](wxCommandEvent&) {
      ed_ctrl->SetValue(cmd);
    });
    preset_row->Add(btn, 0, wxRIGHT, 6);
  };
  add_preset("System Default", "");
  add_preset("VS Code", "code {project} {file}");
  add_preset("Neovim", "nvim {file}");
  add_preset("Sublime", "subl {project} {file}");

  auto* lore_exe_lbl = new wxStaticText(&dlg, wxID_ANY, "Lore Executable:");
  auto* lore_exe_ctrl = new wxTextCtrl(
      &dlg, wxID_ANY, wxString::FromUTF8(scene.Settings().lore_executable));

  auto* lore_srv_lbl = new wxStaticText(&dlg, wxID_ANY, "Lore Server URL:");
  auto* lore_srv_ctrl = new wxTextCtrl(
      &dlg, wxID_ANY, wxString::FromUTF8(scene.Settings().lore_server));

  grid->Add(ed_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(ed_ctrl, 1, wxEXPAND);
  grid->Add(preset_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(preset_row, 1, wxEXPAND);
  grid->Add(lore_exe_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(lore_exe_ctrl, 1, wxEXPAND);
  grid->Add(lore_srv_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(lore_srv_ctrl, 1, wxEXPAND);
  root->Add(grid, 0, wxEXPAND | wxALL, 14);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, "Save Settings");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(560, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return false;

  scene.Settings().external_editor = ed_ctrl->GetValue().ToStdString();
  scene.Settings().lore_executable = lore_exe_ctrl->GetValue().ToStdString();
  scene.Settings().lore_server = lore_srv_ctrl->GetValue().ToStdString();
  scene.Settings().Save();
  return true;
}

UnsavedChoice ShowUnsavedChangesDialog(wxWindow* parent,
                                       const wxString& message) {
  wxDialog dlg(parent, wxID_ANY, "Unsaved Changes", wxDefaultPosition,
               wxSize(420, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* lbl = new wxStaticText(&dlg, wxID_ANY, message);
  lbl->Wrap(380);
  root->Add(lbl, 0, wxALL, 16);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* save = new wxButton(&dlg, wxID_YES, "Save");
  auto* dont_save = new wxButton(&dlg, wxID_NO, "Don't Save");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  save->SetDefault();
  save->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dlg.EndModal(wxID_YES); });
  dont_save->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dlg.EndModal(wxID_NO); });
  cancel->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { dlg.EndModal(wxID_CANCEL); });

  btns->Add(dont_save, 0);
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(save, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(420, dlg.GetSize().y));
  dlg.CenterOnParent();
  const int res = dlg.ShowModal();
  if (res == wxID_YES) return UnsavedChoice::kSave;
  if (res == wxID_NO) return UnsavedChoice::kDontSave;
  return UnsavedChoice::kCancel;
}

bool ShowDeleteConfirmDialog(wxWindow* parent, const fs::path& target) {
  wxDialog dlg(parent, wxID_ANY, "Confirm Delete", wxDefaultPosition,
               wxSize(420, -1));
  auto* root = new wxBoxSizer(wxVERTICAL);

  const wxString kind = fs::is_directory(target) ? "folder" : "file";
  auto* lbl = new wxStaticText(
      &dlg, wxID_ANY,
      wxString::Format("Delete %s '%s' from disk? This cannot be undone.", kind,
                       wxString::FromUTF8(target.filename().string())));
  lbl->Wrap(380);
  root->Add(lbl, 0, wxALL, 16);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* del = new wxButton(&dlg, wxID_OK, "Delete");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  cancel->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(del, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(420, dlg.GetSize().y));
  dlg.CenterOnParent();
  return dlg.ShowModal() == wxID_OK;
}

std::optional<std::string> ShowAddComponentDialog(wxWindow* parent,
                                                  const MapNode& node) {
  wxDialog dlg(parent, wxID_ANY, "Add Component", wxDefaultPosition,
               wxSize(360, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* search = new wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxTE_PROCESS_ENTER);
  search->SetHint("Search components...");
  root->Add(search, 0, wxEXPAND | wxALL, 10);

  auto* list = new wxListCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL |
                                  wxBORDER_SIMPLE);
  list->InsertColumn(0, "Component", wxLIST_FORMAT_LEFT, 320);
  root->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

  auto* info_lbl = new wxStaticText(&dlg, wxID_ANY, wxEmptyString);
  info_lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  root->Add(info_lbl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* new_script = new wxButton(&dlg, wxID_ANY, "New Script...");
  auto* add_btn = new wxButton(&dlg, wxID_OK, "Add");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  add_btn->Enable(false);
  btns->Add(new_script, 0);
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(add_btn, 0);
  root->Add(btns, 0, wxEXPAND | wxALL, 10);

  struct Row {
    bool is_header = false;
    std::string name;
    std::string category;
    std::string description;
  };
  std::vector<Row> rows;
  std::optional<std::string> chosen;

  auto populate = [&]() {
    list->DeleteAllItems();
    rows.clear();
    const std::string needle = Lower(search->GetValue().ToStdString());
    std::string category;
    bool any_scripts = false;
    for (const ComponentInfo* info : TypeRegistry::Instance().Components()) {
      if (node.FindComponent(info->name)) continue;
      if (!needle.empty() && Lower(info->name).find(needle) == std::string::npos) {
        continue;
      }
      if (info->category != category) {
        category = info->category;
        rows.push_back({true, "--- " + category + " ---", category, ""});
      }
      any_scripts |= info->is_script;
      rows.push_back({false, info->name, info->category, info->description});
    }
    if (!any_scripts && needle.empty()) {
      rows.push_back(
          {true, "Scripts: Build the project to list scripts", "Scripts", ""});
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      const long idx =
          list->InsertItem(static_cast<long>(i), wxString::FromUTF8(rows[i].name));
      if (rows[i].is_header) {
        list->SetItemTextColour(
            idx, wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
        list->SetItemFont(idx, list->GetFont().Bold());
      }
    }
    add_btn->Enable(false);
  };

  populate();

  list->Bind(wxEVT_SIZE, [list](wxSizeEvent& e) {
    list->SetColumnWidth(0, std::max(120, list->GetClientSize().x - 16));
    e.Skip();
  });
  list->Bind(wxEVT_LIST_ITEM_SELECTED, [&](wxListEvent& evt) {
    const long idx = evt.GetIndex();
    if (idx >= 0 && static_cast<size_t>(idx) < rows.size() &&
        !rows[idx].is_header) {
      chosen = rows[idx].name;
      add_btn->Enable(true);
      info_lbl->SetLabel(wxString::FromUTF8(rows[idx].description));
    } else {
      chosen.reset();
      add_btn->Enable(false);
      info_lbl->SetLabel(wxEmptyString);
    }
  });
  list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent& evt) {
    const long idx = evt.GetIndex();
    if (idx >= 0 && static_cast<size_t>(idx) < rows.size() &&
        !rows[idx].is_header) {
      chosen = rows[idx].name;
      dlg.EndModal(wxID_OK);
    }
  });
  search->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { populate(); });
  search->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
    for (const auto& r : rows) {
      if (!r.is_header) {
        chosen = r.name;
        dlg.EndModal(wxID_OK);
        return;
      }
    }
  });
  new_script->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    chosen = "__NEW_SCRIPT__";
    dlg.EndModal(wxID_OK);
  });

  search->SetFocus();
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return std::nullopt;
  return chosen;
}

}  // namespace Mercury::EditorUI
