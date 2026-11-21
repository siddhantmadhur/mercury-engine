// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dialogs.h"

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dirdlg.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "cmake_build.h"
#include "editor_icons.h"
#include "os/filesystem.h"
#include "project_trust.h"
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

namespace {

const char* ScriptKindLabel(Templates::ScriptKind kind) {
  switch (kind) {
    case Templates::ScriptKind::kAngelScript:
      return "AngelScript Behaviour";
    case Templates::ScriptKind::kCppBehaviour:
      return "C++ Behaviour";
    case Templates::ScriptKind::kCppSystem:
      return "C++ System";
    case Templates::ScriptKind::kCppEditorSystem:
      return "C++ Editor System";
  }
  return "AngelScript Behaviour";
}

}  // namespace

std::optional<NewScriptRequest> ShowNewScriptDialog(
    wxWindow* parent, EditorScene& scene,
    const std::vector<Templates::ScriptKind>& allowed_kinds) {
  auto project = scene.GetProject();
  if (!project) return std::nullopt;
  const fs::path root = project->GetRoot();
  const EditorSettings& settings = scene.Settings();

  std::vector<Templates::ScriptKind> choices;
  if (allowed_kinds.empty()) {
    choices = {Templates::ScriptKind::kAngelScript,
               Templates::ScriptKind::kCppBehaviour,
               Templates::ScriptKind::kCppSystem,
               Templates::ScriptKind::kCppEditorSystem};
  } else {
    choices = allowed_kinds;
  }
  if (choices.empty()) return std::nullopt;

  int initial = 0;
  for (size_t i = 0; i < choices.size(); i++) {
    if (choices[i] == settings.new_script_kind) {
      initial = static_cast<int>(i);
      break;
    }
  }

  wxDialog dlg(parent, wxID_ANY, "New Script", wxDefaultPosition,
               wxSize(500, -1));
  dlg.SetName("new_script_dialog");
  auto* root_sizer = new wxBoxSizer(wxVERTICAL);

  root_sizer->Add(new wxStaticText(&dlg, wxID_ANY, "Kind:"), 0,
                  wxLEFT | wxRIGHT | wxTOP, 14);
  auto* kind = new wxChoice(&dlg, wxID_ANY);
  kind->SetName("script_kind");
  for (Templates::ScriptKind choice : choices) {
    kind->Append(wxString::FromUTF8(ScriptKindLabel(choice)));
  }
  kind->SetSelection(initial);
  if (choices.size() == 1) kind->Disable();
  root_sizer->Add(kind, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);

  auto* name_lbl =
      new wxStaticText(&dlg, wxID_ANY, "Class Name (AngelScript):");
  root_sizer->Add(name_lbl, 0, wxLEFT | wxRIGHT | wxTOP, 14);

  auto* input = new wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_PROCESS_ENTER);
  input->SetName("script_class_name");
  root_sizer->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);

  auto* preview = new wxStaticText(&dlg, wxID_ANY,
                                   "Enter a valid C++/AngelScript class name");
  // Room for "Will create:" plus a header and a source path, so the live
  // preview does not grow the dialog line by line.
  preview->SetMinSize(wxSize(-1, 62));
  preview->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  root_sizer->Add(preview, 0, wxLEFT | wxRIGHT | wxTOP, 8);

  auto* open_after = new wxCheckBox(&dlg, wxID_ANY, "Open after creating");
  open_after->SetName("script_open_after");
  open_after->SetValue(settings.open_script_after_create);
  root_sizer->Add(open_after, 0, wxLEFT | wxRIGHT | wxTOP, 10);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, "Create");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->Enable(false);
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root_sizer->Add(btns, 0, wxEXPAND | wxALL, 14);

  auto selected_kind = [&]() {
    const int sel = kind->GetSelection();
    if (sel < 0 || static_cast<size_t>(sel) >= choices.size()) {
      return choices.front();
    }
    return choices[static_cast<size_t>(sel)];
  };

  auto update = [&]() {
    const Templates::ScriptKind chosen = selected_kind();
    const bool cpp = Templates::IsCppScript(chosen);
    name_lbl->SetLabel(cpp ? "Class Name (C++):" : "Class Name (AngelScript):");
    name_lbl->GetParent()->Layout();

    const std::string name = input->GetValue().ToStdString();
    auto show_message = [&](const wxString& text, bool error) {
      preview->SetLabel(text);
      preview->SetForegroundColour(
          error ? wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
                : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      preview->SetFont(error ? dlg.GetFont().Bold() : dlg.GetFont());
      preview->GetParent()->Layout();
    };

    if (name.empty()) {
      ok->Enable(false);
      show_message("Enter a valid C++/AngelScript class name", false);
      return;
    }
    const std::string reason = Templates::ValidateScriptName(name, chosen);
    if (!reason.empty()) {
      ok->Enable(false);
      show_message(wxString::FromUTF8(reason), true);
      return;
    }
    std::string folder_error;
    if (os::EnsureProjectContentDir(root, Templates::ScriptKindFolder(chosen),
                                    &folder_error)
            .empty()) {
      ok->Enable(false);
      show_message(wxString::FromUTF8(folder_error), true);
      return;
    }

    auto relative = [&](const fs::path& file) {
      std::error_code rel_ec;
      const fs::path rel = fs::relative(file, root, rel_ec);
      return (rel_ec ? file.filename() : rel).generic_string();
    };

    const Templates::ScriptPaths paths =
        Templates::GetScriptPaths(root, chosen, name);
    std::error_code ec;
    for (const fs::path& file : {paths.header, paths.source}) {
      if (file.empty()) continue;
      if (fs::symlink_status(file, ec).type() != fs::file_type::not_found) {
        ok->Enable(false);
        show_message(wxString::FromUTF8("Already exists: " + relative(file) +
                                        " (choose another name)"),
                     true);
        return;
      }
    }

    wxString text = "Will create:";
    for (const fs::path& file : {paths.header, paths.source}) {
      if (file.empty()) continue;
      text += "\n  " + wxString::FromUTF8(relative(file));
    }
    if (cpp && project->GetBuildTarget().empty()) {
      text += "\n\nWill set [build] target = " +
              wxString::FromUTF8(Templates::ToTargetName(project->GetName())) +
              " so Play builds this project.";
    }
    ok->Enable(true);
    show_message(text, false);
  };

  kind->Bind(wxEVT_CHOICE, [&](wxCommandEvent&) { update(); });
  input->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { update(); });
  input->Bind(wxEVT_TEXT_ENTER, [&](wxCommandEvent&) {
    if (ok->IsEnabled()) dlg.EndModal(wxID_OK);
  });
  update();
  input->SetFocus();

  dlg.SetSizerAndFit(root_sizer);
  dlg.SetSize(wxSize(500, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return std::nullopt;

  NewScriptRequest request;
  request.kind = selected_kind();
  request.class_name = input->GetValue().Strip(wxString::both).ToStdString();
  request.open_after_create = open_after->GetValue();
  return request;
}

bool EnsureNativeProjectTrusted(wxWindow* parent, EditorScene& scene) {
  auto project = scene.GetProject();
  if (!project || project->GetBuildTarget().empty()) return true;
  const fs::path root = project->GetRoot();
  EditorSettings& settings = scene.Settings();
  if (!settings.HasProjectTrustDecision(root)) {
    wxMessageDialog dialog(
        parent,
        "This project builds and runs C++ code from its own folder. Only "
        "trust projects you wrote or obtained from a source you trust.",
        "Trust Project?", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    dialog.SetYesNoLabels("Trust and Continue", "Cancel");
    if (dialog.ShowModal() != wxID_YES) return false;
    SetProjectTrust(settings, root, true, scene.ConsoleRef());
  }
  if (!settings.IsProjectTrusted(root)) {
    wxMessageBox(
        "This project is marked as not trusted. Change the decision in "
        "Project > Trust Editor Systems.",
        "Mercury", wxOK | wxICON_WARNING, parent);
    return false;
  }
  return true;
}

bool ShowReleaseBuildDialog(wxWindow* parent, EditorScene& scene) {
  auto project = scene.GetProject();
  if (!project) return false;
  if (!EnsureNativeProjectTrusted(parent, scene)) return false;

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
  const std::string default_out = project->GetBuildOutput().empty()
                                      ? ReleaseBuild::DefaultOutput(*project)
                                      : project->GetBuildOutput();
  auto* out_ctrl =
      new wxTextCtrl(&dlg, wxID_ANY, wxString::FromUTF8(default_out));
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

  // Rendering options, written to the project's [render] section.
  auto* render_box = new wxStaticBoxSizer(wxVERTICAL, &dlg, "Rendering");
  auto* render_grid = new wxFlexGridSizer(2, 10, 10);
  render_grid->AddGrowableCol(1, 1);
  wxWindow* box = render_box->GetStaticBox();

  wxArrayString aa_choices;
  aa_choices.Add("None");
  aa_choices.Add("FXAA");
  aa_choices.Add("TAA");
  auto* aa_choice =
      new wxChoice(box, wxID_ANY, wxDefaultPosition, wxDefaultSize, aa_choices);
  const std::string aa_mode = project->GetRenderAntiAliasing();
  aa_choice->SetSelection(aa_mode == "none" ? 0 : aa_mode == "fxaa" ? 1 : 2);
  aa_choice->SetToolTip(
      "TAA is the default and the best partner for FSR 1. Filament discourages "
      "running FXAA before FSR, so prefer TAA when upscaling.");

  auto* fsr_check = new wxCheckBox(box, wxID_ANY, "FSR 1 upscaling");
  fsr_check->SetValue(project->GetRenderFsr());
  fsr_check->SetToolTip(
      "Draws the scene smaller and upscales it with AMD FidelityFX Super "
      "Resolution 1. FSR 1 works best with TAA; FXAA before FSR is "
      "discouraged by Filament.");

  wxArrayString quality_choices;
  quality_choices.Add("High");
  quality_choices.Add("Ultra");
  auto* quality_choice = new wxChoice(box, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, quality_choices);
  quality_choice->SetSelection(project->GetRenderFsrQuality() == "high" ? 0
                                                                        : 1);

  auto* scale_slider = new wxSlider(
      box, wxID_ANY,
      static_cast<int>(std::lround(project->GetRenderScale() * 100)), 50, 100,
      wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL | wxSL_LABELS);
  auto* sharpness_slider = new wxSlider(
      box, wxID_ANY,
      static_cast<int>(std::lround(project->GetRenderFsrSharpness() * 100)), 0,
      100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL | wxSL_LABELS);

  auto* quality_lbl = new wxStaticText(box, wxID_ANY, "FSR Quality:");
  auto* scale_lbl = new wxStaticText(box, wxID_ANY, "Render Scale (%):");
  auto* sharpness_lbl = new wxStaticText(box, wxID_ANY, "Sharpness (%):");
  render_grid->Add(new wxStaticText(box, wxID_ANY, "Anti-aliasing:"), 0,
                   wxALIGN_CENTER_VERTICAL);
  render_grid->Add(aa_choice, 1, wxEXPAND);
  render_grid->AddSpacer(0);
  render_grid->Add(fsr_check, 0, wxALIGN_CENTER_VERTICAL);
  render_grid->Add(quality_lbl, 0, wxALIGN_CENTER_VERTICAL);
  render_grid->Add(quality_choice, 1, wxEXPAND);
  render_grid->Add(scale_lbl, 0, wxALIGN_CENTER_VERTICAL);
  render_grid->Add(scale_slider, 1, wxEXPAND);
  render_grid->Add(sharpness_lbl, 0, wxALIGN_CENTER_VERTICAL);
  render_grid->Add(sharpness_slider, 1, wxEXPAND);
  render_box->Add(render_grid, 1, wxEXPAND | wxALL, 8);
  root->Add(render_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  const auto update_fsr_controls = [=]() {
    const bool on = fsr_check->GetValue();
    quality_lbl->Enable(on);
    quality_choice->Enable(on);
    scale_lbl->Enable(on);
    scale_slider->Enable(on);
    sharpness_lbl->Enable(on);
    sharpness_slider->Enable(on);
  };
  update_fsr_controls();
  fsr_check->Bind(wxEVT_CHECKBOX,
                  [=](wxCommandEvent&) { update_fsr_controls(); });

  browse->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    fs::path initial = project->Resolve(out_ctrl->GetValue().ToStdString());
    if (!fs::exists(initial)) initial = project->GetRoot();
    wxDirDialog dir_dlg(&dlg, "Select Build Output Folder",
                        wxString::FromUTF8(initial.string()),
                        wxDD_DEFAULT_STYLE);
    if (dir_dlg.ShowModal() == wxID_OK) {
      out_ctrl->SetValue(wxString::FromUTF8(
          project->ToResPath(dir_dlg.GetPath().ToStdString())));
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
  static constexpr const char* kAntiAliasing[] = {"none", "fxaa", "taa"};
  const int aa_sel = aa_choice->GetSelection();
  project->SetRenderAntiAliasing(
      kAntiAliasing[aa_sel >= 0 && aa_sel < 3 ? aa_sel : 2]);
  project->SetRenderFsr(fsr_check->GetValue());
  project->SetRenderFsrQuality(quality_choice->GetSelection() == 0 ? "high"
                                                                   : "ultra");
  project->SetRenderScale(static_cast<float>(scale_slider->GetValue()) /
                          100.0f);
  project->SetRenderFsrSharpness(
      static_cast<float>(sharpness_slider->GetValue()) / 100.0f);
  if (!project->Save()) {
    wxMessageBox("Could not save the rendering options to the project file.",
                 "Build Game", wxOK | wxICON_WARNING, parent);
  }
  return scene.BuildGame(out_ctrl->GetValue().ToStdString());
}

bool ShowPreferencesDialog(wxWindow* parent, EditorScene& scene) {
  wxDialog dlg(parent, wxID_ANY, "Editor Settings", wxDefaultPosition,
               wxSize(640, -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
  dlg.SetName("preferences_dialog");
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* notebook = new wxNotebook(&dlg, wxID_ANY);
  notebook->SetName("prefs_notebook");

  // ---- General tab: the previous settings page, behaviour unchanged. ----
  auto* general = new wxPanel(notebook);
  auto* general_box = new wxBoxSizer(wxVERTICAL);

  auto* grid = new wxFlexGridSizer(2, 10, 12);
  grid->AddGrowableCol(1, 1);

  auto* open_lbl = new wxStaticText(general, wxID_ANY, "Open Scripts In:");
  auto* open_choice = new wxChoice(general, wxID_ANY);
  open_choice->Append("Built-in code editor");
  open_choice->Append("External editor");
  open_choice->SetSelection(scene.Settings().open_scripts_in_editor ? 0 : 1);

  auto* ed_lbl = new wxStaticText(general, wxID_ANY, "External Script Editor:");
  auto* ed_ctrl = new wxTextCtrl(
      general, wxID_ANY, wxString::FromUTF8(scene.Settings().external_editor));
  ed_ctrl->SetToolTip(
      "Command template with {project} and {file} placeholders. Leave empty "
      "for system default app.");

  auto* preset_lbl = new wxStaticText(general, wxID_ANY, "Presets:");
  auto* preset_row = new wxBoxSizer(wxHORIZONTAL);
  auto add_preset = [&](const char* name, const char* cmd) {
    auto* btn = new wxButton(general, wxID_ANY, name);
    ApplyButtonPadding(btn, 8);
    btn->Bind(wxEVT_BUTTON,
              [ed_ctrl, cmd](wxCommandEvent&) { ed_ctrl->SetValue(cmd); });
    preset_row->Add(btn, 0, wxRIGHT, 6);
  };
  add_preset("System Default", "");
  add_preset("VS Code", "code {project} {file}");
  add_preset("Neovim", "nvim {file}");
  add_preset("Sublime", "subl {project} {file}");

  auto* vim_lbl = new wxStaticText(general, wxID_ANY, "Keybindings:");
  auto* vim_check = new wxCheckBox(general, wxID_ANY,
                                   "Enable Vim mode in the built-in editor");
  vim_check->SetValue(scene.Settings().vim_mode);

#if defined(__APPLE__)
  wxStaticText* hud_lbl = nullptr;
  wxCheckBox* hud_check = nullptr;
  if (cmake_build::IsDebugBuild()) {
    hud_lbl = new wxStaticText(general, wxID_ANY, "Debug Play FPS:");
    hud_check = new wxCheckBox(general, wxID_ANY,
                               "Show the Metal performance HUD in the game");
    hud_check->SetValue(scene.Settings().show_metal_hud);
    hud_check->SetToolTip(
        "Sets MTL_HUD_ENABLED=1 for the game process, which draws Metal's "
        "frame-rate and GPU statistics overlay. Debug builds only.");
  }
#endif

  auto* lore_exe_lbl = new wxStaticText(general, wxID_ANY, "Lore Executable:");
  auto* lore_exe_ctrl = new wxTextCtrl(
      general, wxID_ANY, wxString::FromUTF8(scene.Settings().lore_executable));

  auto* lore_srv_lbl = new wxStaticText(general, wxID_ANY, "Lore Server URL:");
  auto* lore_srv_ctrl = new wxTextCtrl(
      general, wxID_ANY, wxString::FromUTF8(scene.Settings().lore_server));

  grid->Add(open_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(open_choice, 0, wxEXPAND);
  grid->Add(ed_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(ed_ctrl, 1, wxEXPAND);
  grid->Add(preset_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(preset_row, 1, wxEXPAND);
  grid->Add(vim_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(vim_check, 0);
#if defined(__APPLE__)
  if (hud_check) {
    grid->Add(hud_lbl, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(hud_check, 0);
  }
#endif
  grid->Add(lore_exe_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(lore_exe_ctrl, 1, wxEXPAND);
  grid->Add(lore_srv_lbl, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(lore_srv_ctrl, 1, wxEXPAND);
  general_box->Add(grid, 0, wxEXPAND | wxALL, 14);

  auto sync_external_enabled = [&]() {
    const bool external = open_choice->GetSelection() == 1;
    ed_ctrl->Enable(external);
    preset_row->ShowItems(external);
    general->Layout();
  };
  open_choice->Bind(wxEVT_CHOICE,
                    [&](wxCommandEvent&) { sync_external_enabled(); });
  sync_external_enabled();

  general->SetSizer(general_box);
  notebook->AddPage(general, "General");

  // ---- MCP tab: the local MCP server for AI tools (Claude Code). ----
  auto* mcp = new wxPanel(notebook);
  auto* mcp_box = new wxBoxSizer(wxVERTICAL);

  auto* enable_check = new wxCheckBox(mcp, wxID_ANY, "Enable MCP server");
  enable_check->SetName("mcp_enable");
  enable_check->SetValue(scene.Settings().mcp_enabled);
  enable_check->SetToolTip(
      "Exposes the open project to AI tools over a local HTTP endpoint "
      "(127.0.0.1). The server runs while the editor is open.");

  auto* mcp_grid = new wxFlexGridSizer(2, 8, 10);
  mcp_grid->AddGrowableCol(1, 1);

  auto* port_lbl = new wxStaticText(mcp, wxID_ANY, "Port:");
  auto* port_row = new wxBoxSizer(wxHORIZONTAL);
  auto* port_ctrl = new wxSpinCtrl(
      mcp, wxID_ANY, "49152", wxDefaultPosition, wxDefaultSize,
      wxSP_ARROW_KEYS, 1024, 65535,
      std::clamp(scene.Settings().mcp_port, 1024, 65535));
  port_ctrl->SetName("mcp_port");
  port_ctrl->SetToolTip("Fixed port for the MCP server.");
  auto* ephemeral_check =
      new wxCheckBox(mcp, wxID_ANY, "Pick a free port automatically");
  ephemeral_check->SetName("mcp_ephemeral");
  ephemeral_check->SetValue(scene.Settings().mcp_ephemeral);
  ephemeral_check->SetToolTip(
      "The server picks an unused port each time it starts; the fixed Port "
      "value is ignored. Ports below 1024 are never used.");
  port_row->Add(port_ctrl, 0, wxRIGHT, 8);
  port_row->Add(ephemeral_check, 0, wxALIGN_CENTER_VERTICAL);

  auto* access_lbl = new wxStaticText(mcp, wxID_ANY, "Access Mode:");
  auto* access_choice = new wxChoice(mcp, wxID_ANY);
  access_choice->SetName("mcp_access");
  access_choice->Append("Read-Only");
  access_choice->Append("Allow Scene Edits");
  access_choice->SetSelection(scene.Settings().mcp_allow_edits ? 1 : 0);
  access_choice->SetToolTip(
      "Allow Scene Edits lets MCP clients create and change scene nodes "
      "(opt in; the default is read-only).");

  mcp_grid->Add(port_lbl, 0, wxALIGN_CENTER_VERTICAL);
  mcp_grid->Add(port_row, 1, wxEXPAND);
  mcp_grid->Add(access_lbl, 0, wxALIGN_CENTER_VERTICAL);
  mcp_grid->Add(access_choice, 1, wxEXPAND);

  auto* write_config_check =
      new wxCheckBox(mcp, wxID_ANY, "Auto-write .mcp.json");
  write_config_check->SetName("mcp_write_config");
  write_config_check->SetValue(scene.Settings().mcp_write_config);
  write_config_check->SetToolTip(
      "While the server runs, adds a 'mercury' entry (URL + bearer token) to "
      ".mcp.json in the project folder so Claude Code finds it, and removes "
      "it when the project closes. The file contains a local token: keep it "
      "out of version control.");

  auto* status = new wxStaticText(mcp, wxID_ANY, wxEmptyString);
  status->SetName("mcp_status");
  // Room for the longest state so the layout below does not jump.
  status->SetMinSize(wxSize(-1, status->GetCharHeight() + 4));

  auto* url_ctrl = new wxTextCtrl(mcp, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_READONLY);
  url_ctrl->SetName("mcp_url");
  url_ctrl->SetToolTip(
      "The MCP endpoint URL. With \"Pick a free port automatically\", the "
      "port is chosen when the server starts.");
  auto* url_copy = new wxButton(mcp, wxID_ANY, "Copy");
  auto* url_row = new wxBoxSizer(wxHORIZONTAL);
  url_row->Add(url_ctrl, 1, wxEXPAND | wxRIGHT, 6);
  url_row->Add(url_copy, 0, wxALIGN_CENTER_VERTICAL);

  auto* command_ctrl = new wxTextCtrl(mcp, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY |
                                          wxTE_NO_VSCROLL | wxBORDER_SIMPLE);
  command_ctrl->SetName("mcp_command");
  command_ctrl->SetFont(MonospaceFont());
  command_ctrl->SetMinSize(wxSize(-1, command_ctrl->GetCharHeight() * 3));
  auto* command_copy = new wxButton(mcp, wxID_ANY, "Copy");
  auto* command_row = new wxBoxSizer(wxHORIZONTAL);
  command_row->Add(command_ctrl, 1, wxEXPAND | wxRIGHT, 6);
  command_row->Add(command_copy, 0, wxALIGN_CENTER_VERTICAL);

  auto* regen_btn = new wxButton(mcp, wxID_ANY, "Regenerate Token");
  regen_btn->SetName("mcp_regen");
  regen_btn->SetToolTip(
      "Takes effect immediately. Generates a new bearer token: the running "
      "server restarts and .mcp.json is rewritten, so clients registered "
      "with the old command stop working until you copy the command again.");

  auto copy_text = [](const wxString& text) {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(new wxTextDataObject(text));
      // Flush keeps the text on the clipboard after this process exits
      // (X11 loses clipboard content owned by a dead process otherwise).
      wxTheClipboard->Flush();
      wxTheClipboard->Close();
    }
  };
  url_copy->Bind(
      wxEVT_BUTTON,
      [url_ctrl, copy_text](wxCommandEvent&) { copy_text(url_ctrl->GetValue()); });
  command_copy->Bind(wxEVT_BUTTON, [command_ctrl, copy_text](wxCommandEvent&) {
    copy_text(command_ctrl->GetValue());
  });

  // With "Pick a free port automatically" on and nothing running, the port
  // is not known yet, so there is no real URL to copy.
  const wxString kEphemeralPlaceholder =
      "http://127.0.0.1:<chosen when the server starts>/mcp";
  auto current_url = [&]() -> wxString {
    if (scene.Mcp().Running()) {
      const wxString live = wxString::FromUTF8(scene.Mcp().Url());
      if (!live.empty()) return live;
    }
    if (ephemeral_check->GetValue()) return kEphemeralPlaceholder;
    return "http://127.0.0.1:" + std::to_string(port_ctrl->GetValue()) +
           "/mcp";
  };
  auto has_real_url = [&]() {
    if (!ephemeral_check->GetValue()) return true;
    return scene.Mcp().Running() &&
           !wxString::FromUTF8(scene.Mcp().Url()).empty();
  };
  auto command_line = [&]() {
    std::string token = scene.Settings().mcp_token;
    if (token.empty()) token = "<generated when the server is enabled>";
    return wxString::Format(
        "claude mcp add --transport http mercury %s --header "
        "\"Authorization: Bearer %s\"",
        current_url(), token);
  };
  auto refresh_mcp = [&]() {
    port_ctrl->Enable(!ephemeral_check->GetValue());
    const bool real_url = has_real_url();
    url_ctrl->SetValue(current_url());
    command_ctrl->SetValue(command_line());
    url_copy->Enable(real_url);
    command_copy->Enable(real_url);
    // The token exists only once the host generated it (first enable); an
    // empty token means nothing has been issued to rotate yet.
    regen_btn->Enable(!scene.Settings().mcp_token.empty());
    if (scene.Mcp().Running()) {
      status->SetLabel("Running at " +
                       wxString::FromUTF8(scene.Mcp().Url()));
      status->SetForegroundColour(
          wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    } else {
      const std::string error = scene.Mcp().LastError();
      if (error.empty()) {
        status->SetLabel("Stopped");
        status->SetForegroundColour(
            wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      } else {
        status->SetLabel("Error: " + wxString::FromUTF8(error));
        status->SetForegroundColour(Colors::kAxisX);
      }
    }
  };
  regen_btn->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
    scene.Mcp().RegenerateToken();
    refresh_mcp();
  });
  ephemeral_check->Bind(wxEVT_CHECKBOX,
                        [&](wxCommandEvent&) { refresh_mcp(); });
  port_ctrl->Bind(wxEVT_SPINCTRL, [&](wxSpinEvent&) { refresh_mcp(); });
  port_ctrl->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { refresh_mcp(); });

  // The server state can change while the dialog is open; follow it.
  wxTimer status_timer;
  status_timer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) { refresh_mcp(); });
  status_timer.Start(1000);
  refresh_mcp();

  mcp_box->Add(enable_check, 0, wxALL, 14);
  mcp_box->Add(mcp_grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);
  mcp_box->Add(write_config_check, 0, wxLEFT | wxRIGHT, 14);
  mcp_box->Add(status, 0, wxLEFT | wxRIGHT | wxTOP, 14);
  mcp_box->Add(url_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);
  mcp_box->Add(command_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 14);
  mcp_box->Add(new wxStaticText(
                   mcp, wxID_ANY,
                   "Run /mcp in Claude Code to verify the connection."),
               0, wxLEFT | wxRIGHT | wxTOP, 10);
  mcp_box->Add(regen_btn, 0, wxLEFT | wxRIGHT | wxTOP, 10);
  mcp_box->AddSpacer(4);
  mcp->SetSizer(mcp_box);
  notebook->AddPage(mcp, "MCP");

  root->Add(notebook, 1, wxEXPAND | wxALL, 14);

  auto* btns = new wxBoxSizer(wxHORIZONTAL);
  auto* ok = new wxButton(&dlg, wxID_OK, "Save Settings");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  ok->SetDefault();
  btns->AddStretchSpacer();
  btns->Add(cancel, 0, wxRIGHT, 8);
  btns->Add(ok, 0);
  root->Add(btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 14);

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(640, dlg.GetSize().y));
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return false;

  scene.Settings().open_scripts_in_editor = open_choice->GetSelection() == 0;
  scene.Settings().vim_mode = vim_check->GetValue();
#if defined(__APPLE__)
  if (hud_check) scene.Settings().show_metal_hud = hud_check->GetValue();
#endif
  scene.Settings().external_editor = ed_ctrl->GetValue().ToStdString();
  scene.Settings().lore_executable = lore_exe_ctrl->GetValue().ToStdString();
  scene.Settings().lore_server = lore_srv_ctrl->GetValue().ToStdString();
  // The MCP host owns the token (it generates the first one on enable); the
  // dialog only forwards the user's choices.
  scene.Settings().mcp_enabled = enable_check->GetValue();
  scene.Settings().mcp_port = port_ctrl->GetValue();
  scene.Settings().mcp_ephemeral = ephemeral_check->GetValue();
  scene.Settings().mcp_allow_edits = access_choice->GetSelection() == 1;
  scene.Settings().mcp_write_config = write_config_check->GetValue();
  scene.Settings().Save();
  scene.Mcp().ApplySettings();
  return true;
}

std::optional<bool> ShowTrustSystemsDialog(
    wxWindow* parent, const std::filesystem::path& root,
    const std::string& systems_target, const std::filesystem::path& source_dir,
    bool currently_trusted, bool first_time) {
  wxDialog dlg(parent, wxID_ANY,
               first_time ? "Trust Editor Systems?" : "Editor System Trust",
               wxDefaultPosition, wxSize(600, -1));
  auto* sizer = new wxBoxSizer(wxVERTICAL);

  wxString message =
      "This project contains editor systems (native code). Building and "
      "loading them runs arbitrary code with your user permissions.\n\n"
      "Only trust projects you trust. The decision is remembered for future "
      "changes to this project; you can change it later with Project > Trust "
      "Editor Systems.";
  if (!first_time) {
    message += currently_trusted ? "\n\nThis project is currently trusted."
                                 : "\n\nThis project is currently not trusted.";
  }
  auto* label = new wxStaticText(&dlg, wxID_ANY, message);
  label->Wrap(560);
  sizer->Add(label, 0, wxALL, 16);

  auto* path =
      new wxStaticText(&dlg, wxID_ANY, wxString::FromUTF8(root.string()),
                       wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
  path->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  sizer->Add(path, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

  const wxString build_info = wxString::Format(
      "Systems target: %s\nSource: %s",
      systems_target.empty() ? "(none)" : systems_target.c_str(),
      source_dir.string().c_str());
  auto* build_label = new wxStaticText(&dlg, wxID_ANY, build_info);
  build_label->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  sizer->Add(build_label, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  auto* trust = new wxButton(&dlg, wxID_YES, "Trust");
  auto* distrust = new wxButton(&dlg, wxID_NO, "Don't Trust");
  auto* cancel = new wxButton(&dlg, wxID_CANCEL, "Cancel");
  cancel->SetDefault();
  buttons->AddStretchSpacer();
  buttons->Add(cancel, 0, wxRIGHT, 8);
  buttons->Add(distrust, 0, wxRIGHT, 8);
  buttons->Add(trust, 0);
  sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 16);

  dlg.SetSizerAndFit(sizer);
  dlg.SetSize(wxSize(560, dlg.GetSize().y));
  dlg.CenterOnParent();

  switch (dlg.ShowModal()) {
    case wxID_YES:
      return true;
    case wxID_NO:
      return false;
    default:
      return std::nullopt;
  }
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
  dont_save->Bind(wxEVT_BUTTON,
                  [&](wxCommandEvent&) { dlg.EndModal(wxID_NO); });
  cancel->Bind(wxEVT_BUTTON,
               [&](wxCommandEvent&) { dlg.EndModal(wxID_CANCEL); });

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

  auto* search =
      new wxTextCtrl(&dlg, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxDefaultSize, wxTE_PROCESS_ENTER);
  search->SetHint("Search components...");
  root->Add(search, 0, wxEXPAND | wxALL, 10);

  auto* list = new wxListCtrl(
      &dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
  list->InsertColumn(0, "Component", wxLIST_FORMAT_LEFT, 320);
  root->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

  auto* info_lbl = new wxStaticText(&dlg, wxID_ANY, wxEmptyString);
  info_lbl->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
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
      if (!needle.empty() &&
          Lower(info->name).find(needle) == std::string::npos) {
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
      const long idx = list->InsertItem(static_cast<long>(i),
                                        wxString::FromUTF8(rows[i].name));
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

  dlg.SetSizerAndFit(root);
  dlg.SetSize(wxSize(420, 460));
  search->SetFocus();
  dlg.CenterOnParent();
  if (dlg.ShowModal() != wxID_OK) return std::nullopt;
  return chosen;
}

}  // namespace Mercury::EditorUI
