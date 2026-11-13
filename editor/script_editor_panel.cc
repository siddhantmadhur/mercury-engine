/*
 * script_editor_panel.cc
 */
#include "script_editor_panel.h"

#include <wx/filename.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/statbox.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "dialogs.h"
#include "editor_scene.h"
#include "project/project.h"
#include "script_editor_tab.h"
#include "scripting/as/script_builder.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace {

std::string LowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool IsDescendantOf(const wxWindow* window, const wxWindow* ancestor) {
  for (const wxWindow* current = window; current;
       current = current->GetParent()) {
    if (current == ancestor) return true;
    if (current->IsTopLevel()) break;
  }
  return false;
}

std::string ReadText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

class SymbolPickerDialog : public wxDialog {
 public:
  SymbolPickerDialog(wxWindow* parent, std::vector<ScriptSymbol> symbols)
      : wxDialog(parent, wxID_ANY, "Go to Method", wxDefaultPosition,
                 wxSize(700, 460), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
        symbols_(std::move(symbols)) {
    auto* root = new wxBoxSizer(wxVERTICAL);
    search_ = new wxSearchCtrl(this, wxID_ANY);
    search_->ShowCancelButton(true);
    search_->SetDescriptiveText("Filter methods and functions");
    root->Add(search_, 0, wxEXPAND | wxALL, 8);
    list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0,
                          nullptr, wxLB_SINGLE | wxLB_NEEDED_SB);
    root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* cancel = new wxButton(this, wxID_CANCEL, "Cancel");
    auto* ok = new wxButton(this, wxID_OK, "Go");
    ok->SetDefault();
    buttons->AddStretchSpacer();
    buttons->Add(cancel, 0, wxRIGHT, 8);
    buttons->Add(ok, 0);
    root->Add(buttons, 0, wxEXPAND | wxALL, 8);
    SetSizer(root);
    search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { Filter(); });
    list_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) {
      if (list_->GetSelection() != wxNOT_FOUND) EndModal(wxID_OK);
    });
    Filter();
    search_->SetFocus();
  }

  const std::string& SelectedFile() const { return selected_file_; }
  int SelectedLine() const { return selected_line_; }

  bool TransferDataFromWindow() override {
    const int selection = list_->GetSelection();
    if (selection == wxNOT_FOUND ||
        selection >= static_cast<int>(filtered_.size()))
      return false;
    const ScriptSymbol& symbol = symbols_[filtered_[selection]];
    selected_file_ = symbol.file;
    selected_line_ = symbol.line;
    return true;
  }

 private:
  void Filter() {
    const std::string needle =
        LowerAscii(search_->GetValue().utf8_str().data());
    list_->Clear();
    filtered_.clear();
    for (size_t i = 0; i < symbols_.size(); ++i) {
      const ScriptSymbol& symbol = symbols_[i];
      std::string label = symbol.name;
      if (!symbol.owner.empty()) label = symbol.owner + "::" + label;
      if (!needle.empty() &&
          LowerAscii(label).find(needle) == std::string::npos)
        continue;
      label += "    ";
      label += symbol.file;
      if (symbol.line > 0) label += ":" + std::to_string(symbol.line);
      filtered_.push_back(i);
      list_->Append(wxString::FromUTF8(label));
    }
    if (!filtered_.empty()) list_->SetSelection(0);
  }

  std::vector<ScriptSymbol> symbols_;
  std::vector<size_t> filtered_;
  wxSearchCtrl* search_ = nullptr;
  wxListBox* list_ = nullptr;
  std::string selected_file_;
  int selected_line_ = 0;
};

}  // namespace

ScriptEditorPanel::ScriptEditorPanel(wxWindow* parent,
                                     Mercury::EditorScene& scene)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE),
      scene_(scene),
      lint_timer_(this) {
  BuildUi();
  Bind(wxEVT_TIMER, &ScriptEditorPanel::OnLintTimer, this, lint_timer_.GetId());
  language_.Initialize();
  RefreshScriptList();
  ShowWelcomeIfEmpty();
  UpdateStatus();
}

ScriptEditorPanel::~ScriptEditorPanel() { lint_timer_.Stop(); }

void ScriptEditorPanel::BuildUi() {
  auto* root = new wxBoxSizer(wxVERTICAL);
  auto* outer =
      new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_BORDER);

  auto* left = new wxPanel(outer);
  auto* left_sizer = new wxBoxSizer(wxVERTICAL);
  script_filter_ = new wxSearchCtrl(left, wxID_ANY);
  script_filter_->ShowCancelButton(true);
  script_filter_->SetDescriptiveText("Filter scripts");
  script_filter_->SetMinSize(wxSize(-1, 28));
  left_sizer->Add(script_filter_, 0, wxEXPAND | wxALL, 6);
  script_tree_ = new wxTreeCtrl(
      left, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT | wxTR_SINGLE);
  left_sizer->Add(script_tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  auto* refresh = new wxButton(left, wxID_ANY, "Refresh Scripts");
  refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { RefreshScriptList(); });
  left_sizer->Add(refresh, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  left->SetSizer(left_sizer);

  auto* inner =
      new wxSplitterWindow(outer, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_LIVE_UPDATE | wxSP_3DSASH | wxSP_BORDER);
  tabs_ = new wxAuiNotebook(
      inner, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxAUI_NB_TOP | wxAUI_NB_TAB_MOVE | wxAUI_NB_CLOSE_ON_ACTIVE_TAB |
          wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_WINDOWLIST_BUTTON);

  auto* sidebar = new wxPanel(inner);
  auto* side_sizer = new wxBoxSizer(wxVERTICAL);
  auto* side_book = new wxNotebook(sidebar, wxID_ANY);
  outline_ = new wxTreeCtrl(
      side_book, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT | wxTR_SINGLE);
  problems_ =
      new wxListCtrl(side_book, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  problems_->InsertColumn(0, "Location", wxLIST_FORMAT_LEFT, 150);
  problems_->InsertColumn(1, "Message", wxLIST_FORMAT_LEFT, 500);
  side_book->AddPage(outline_, "Outline", true);
  side_book->AddPage(problems_, "Problems");
  side_sizer->Add(side_book, 1, wxEXPAND);
  sidebar->SetSizer(side_sizer);

  inner->SplitVertically(tabs_, sidebar, -270);
  inner->SetMinimumPaneSize(160);
  outer->SplitVertically(left, inner, 220);
  outer->SetMinimumPaneSize(150);
  root->Add(outer, 1, wxEXPAND);

  find_bar_ = new wxPanel(this);
  auto* find_sizer = new wxBoxSizer(wxHORIZONTAL);
  find_text_ =
      new wxSearchCtrl(find_bar_, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxSize(220, -1), wxTE_PROCESS_ENTER);
  find_text_->ShowCancelButton(true);
  find_text_->SetDescriptiveText("Find");
  find_case_ = new wxCheckBox(find_bar_, wxID_ANY, "Aa");
  find_regex_ = new wxCheckBox(find_bar_, wxID_ANY, ".*");
  auto* prev = new wxButton(find_bar_, wxID_ANY, "Prev");
  auto* next = new wxButton(find_bar_, wxID_ANY, "Next");
  auto* close = new wxButton(find_bar_, wxID_ANY, "x");
  replace_text_ =
      new wxTextCtrl(find_bar_, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(180, -1), wxTE_PROCESS_ENTER);
  replace_button_ = new wxButton(find_bar_, wxID_ANY, "Replace");
  replace_all_button_ = new wxButton(find_bar_, wxID_ANY, "All");
  find_sizer->Add(find_text_, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
  find_sizer->Add(find_case_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  find_sizer->Add(find_regex_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  find_sizer->Add(prev, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
  find_sizer->Add(next, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  find_sizer->Add(replace_text_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
  find_sizer->Add(replace_button_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
  find_sizer->Add(replace_all_button_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  find_sizer->AddStretchSpacer();
  find_sizer->Add(close, 0, wxALIGN_CENTER_VERTICAL | wxALL, 4);
  find_bar_->SetSizer(find_sizer);
  find_bar_->Hide();
  root->Add(find_bar_, 0, wxEXPAND);

  command_line_ =
      new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                     wxSize(-1, 26), wxTE_PROCESS_ENTER);
  command_line_->SetFont(MonospaceFont());
  command_line_->Hide();
  root->Add(command_line_, 0, wxEXPAND);
  status_ = new wxStaticText(this, wxID_ANY, "No script open");
  root->Add(status_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 6);

  SetSizer(root);
  SetMinSize(wxSize(420, 220));

  script_filter_->Bind(wxEVT_TEXT,
                       [this](wxCommandEvent&) { RebuildScriptTree(); });
  script_tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED,
                     &ScriptEditorPanel::OnScriptActivated, this);
  outline_->Bind(wxEVT_TREE_ITEM_ACTIVATED,
                 &ScriptEditorPanel::OnOutlineActivated, this);
  problems_->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                  &ScriptEditorPanel::OnProblemActivated, this);
  tabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ScriptEditorPanel::OnTabChanged,
              this);
  tabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSE, &ScriptEditorPanel::OnTabClosing,
              this);
  tabs_->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSED, &ScriptEditorPanel::OnTabClosed,
              this);
  tabs_->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP, [this](wxAuiNotebookEvent& evt) {
    auto* tab =
        dynamic_cast<ScriptEditorTab*>(tabs_->GetPage(evt.GetSelection()));
    if (!tab) return;
    wxMenu menu;
    wxMenuItem* close = menu.Append(wxID_ANY, "Close");
    wxMenuItem* close_others = menu.Append(wxID_ANY, "Close Others");
    menu.AppendSeparator();
    wxMenuItem* copy = menu.Append(wxID_ANY, "Copy Script Path");
    wxMenuItem* reveal = menu.Append(wxID_ANY, "Reveal in Project Browser");
    menu.Bind(
        wxEVT_MENU,
        [this, tab](wxCommandEvent&) { TabCloseRequested(tab, false); },
        close->GetId());
    menu.Bind(
        wxEVT_MENU, [this, tab](wxCommandEvent&) { CloseOtherTabs(tab); },
        close_others->GetId());
    menu.Bind(
        wxEVT_MENU, [this, tab](wxCommandEvent&) { CopyPathToClipboard(tab); },
        copy->GetId());
    menu.Bind(
        wxEVT_MENU,
        [this, tab](wxCommandEvent&) { RevealInProjectBrowser(tab); },
        reveal->GetId());
    PopupMenu(&menu, ScreenToClient(wxGetMousePosition()));
  });

  find_text_->Bind(wxEVT_TEXT_ENTER,
                   [this](wxCommandEvent&) { FindNext(false); });
  find_text_->Bind(wxEVT_SEARCH, [this](wxCommandEvent&) { FindNext(false); });
  prev->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(true); });
  next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(false); });
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    find_bar_->Hide();
    Layout();
    if (active_tab_) active_tab_->FocusEditor();
  });
  replace_text_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
    if (!active_tab_) return;
    wxStyledTextCtrl* editor = active_tab_->Editor();
    const wxString replacement = replace_text_->GetValue();
    if (!editor->GetSelectedText().empty()) {
      editor->ReplaceSelection(replacement);
    }
    FindNext(false);
  });
  replace_button_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (!active_tab_) return;
    wxStyledTextCtrl* editor = active_tab_->Editor();
    if (editor->GetSelectedText().empty()) {
      FindNext(false);
      return;
    }
    editor->ReplaceSelection(replace_text_->GetValue());
    FindNext(false);
  });
  replace_all_button_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (!active_tab_) return;
    wxStyledTextCtrl* editor = active_tab_->Editor();
    const wxString needle = find_text_->GetValue();
    if (needle.empty()) return;
    int flags = 0;
    if (find_case_->GetValue()) flags |= wxSTC_FIND_MATCHCASE;
    if (find_regex_->GetValue()) flags |= wxSTC_FIND_REGEXP;
    editor->SetSearchFlags(flags);
    int count = 0;
    int start = 0;
    while (start <= editor->GetTextLength()) {
      editor->SetTargetStart(start);
      editor->SetTargetEnd(editor->GetTextLength());
      const int found = editor->SearchInTarget(needle);
      if (found < 0) break;
      const int end = editor->GetTargetEnd();
      if (end <= found) break;
      editor->SetTargetStart(found);
      editor->SetTargetEnd(end);
      editor->ReplaceTarget(replace_text_->GetValue());
      start = found + static_cast<int>(replace_text_->GetValue().length());
      ++count;
    }
    SetStatusMessage(wxString::Format("Replaced %d occurrence(s)", count));
  });

  command_line_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
    const wxString value = command_line_->GetValue();
    auto callback = std::move(command_line_submit_);
    HideCommandLine();
    if (callback) callback(value);
  });
  command_line_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
    if (evt.GetKeyCode() == WXK_ESCAPE) {
      HideCommandLine();
      return;
    }
    evt.Skip();
  });
}

void ScriptEditorPanel::RefreshScriptList() {
  auto project = scene_.GetProject();
  last_project_files_revision_ = scene_.ScriptFilesRevision();
  workspace_.Clear();
  script_tree_->DeleteAllItems();
  if (!project) {
    UpdateStatus();
    return;
  }
  const auto files = ScriptBuilder::ScriptFiles(project->GetRoot());
  for (const auto& file : files) {
    const std::string res = RelativePath(file);
    workspace_.SetFile(res, ReadText(file));
  }
  for (ScriptEditorTab* tab : Tabs()) {
    workspace_.SetFile(tab->ResPath(),
                       tab->Editor()->GetText().utf8_str().data());
  }
  RebuildScriptTree();
}

void ScriptEditorPanel::Sync() {
  if (last_project_files_revision_ != scene_.ScriptFilesRevision()) {
    RefreshScriptList();
  }
}

void ScriptEditorPanel::RebuildScriptTree() {
  auto project = scene_.GetProject();
  if (!project) return;
  const std::string filter =
      LowerAscii(script_filter_->GetValue().utf8_str().data());
  script_tree_->DeleteAllItems();
  const wxTreeItemId root = script_tree_->AddRoot("Scripts");
  std::map<std::string, wxTreeItemId> dirs;
  const std::filesystem::path scripts_root = project->GetRoot() / "scripts";
  for (const auto& file : ScriptBuilder::ScriptFiles(project->GetRoot())) {
    const std::string res = RelativePath(file);
    if (!filter.empty() && LowerAscii(res).find(filter) == std::string::npos)
      continue;
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(file, scripts_root, ec);
    if (!filter.empty() || ec) {
      script_tree_->AppendItem(root, wxString::FromUTF8(res), -1, -1,
                               new ScriptTreeItem(file));
      continue;
    }
    wxTreeItemId parent = root;
    std::filesystem::path prefix;
    for (const auto& part : relative.parent_path()) {
      prefix /= part;
      const std::string key = prefix.generic_string();
      auto it = dirs.find(key);
      if (it == dirs.end()) {
        const wxTreeItemId item =
            script_tree_->AppendItem(parent, wxString::FromUTF8(part.string()));
        dirs.emplace(key, item);
        parent = item;
      } else {
        parent = it->second;
      }
    }
    script_tree_->AppendItem(parent,
                             wxString::FromUTF8(relative.filename().string()),
                             -1, -1, new ScriptTreeItem(file));
  }
  script_tree_->ExpandAll();
}

void ScriptEditorPanel::OnScriptActivated(wxTreeEvent& evt) {
  auto* item =
      dynamic_cast<ScriptTreeItem*>(script_tree_->GetItemData(evt.GetItem()));
  if (!item) return;
  OpenFile(item->path);
  evt.Skip();
}

void ScriptEditorPanel::OpenFile(const std::filesystem::path& file, int line) {
  auto project = scene_.GetProject();
  if (!project) return;
  std::error_code ec;
  const std::filesystem::path absolute =
      std::filesystem::weakly_canonical(file, ec);
  const std::string res = RelativePath(ec ? file : absolute);
  auto it = tabs_by_path_.find(res);
  if (it != tabs_by_path_.end()) {
    SelectTab(it->second);
    if (line > 0) it->second->GoTo(line);
    return;
  }
  auto* tab = new ScriptEditorTab(tabs_, this, ec ? file : absolute, res);
  std::string error;
  if (!tab->Load(&error)) {
    wxMessageBox(wxString::FromUTF8(error), "Open Script", wxOK | wxICON_ERROR,
                 this);
    tab->Destroy();
    return;
  }
  tab->SetVimEnabled(scene_.Settings().vim_mode);
  tabs_by_path_[res] = tab;
  welcome_shown_ = false;
  if (welcome_panel_ && tabs_->GetPageCount() > 0 &&
      tabs_->GetPage(0) == welcome_panel_) {
    tabs_->DeletePage(0);
    welcome_panel_ = nullptr;
  }
  const int page = tabs_->GetPageCount();
  tabs_->AddPage(tab, TabTitle(tab), true);
  tabs_->SetPageToolTip(page, wxString::FromUTF8(res));
  workspace_.SetFile(res, tab->Editor()->GetText().utf8_str().data());
  SelectTab(tab);
  if (line > 0)
    tab->GoTo(line);
  else
    tab->FocusEditor();
  RebuildOutline();
  RequestLint();
}

void ScriptEditorPanel::OpenLocation(const std::string& res_path, int line) {
  if (res_path.empty()) return;
  OpenFile(AbsolutePath(res_path), line);
}

void ScriptEditorPanel::SelectTab(ScriptEditorTab* tab) {
  if (!tab) return;
  const int page = tabs_->GetPageIndex(tab);
  if (page == wxNOT_FOUND) return;
  tabs_->SetSelection(page);
  active_tab_ = tab;
  status_message_.clear();
  ParseActiveIntoWorkspace();
  RebuildOutline();
  UpdateStatus();
  tab->FocusEditor();
}

void ScriptEditorPanel::ShowWelcomeIfEmpty() {
  if (!tabs_by_path_.empty() || welcome_shown_) return;
  welcome_panel_ = new wxPanel(tabs_);
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  auto* label = new wxStaticText(
      welcome_panel_, wxID_ANY,
      "Open a script from the Project Browser or the Scripts list.");
  label->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  sizer->AddStretchSpacer();
  sizer->Add(label, 0, wxALIGN_CENTER);
  sizer->AddStretchSpacer();
  welcome_panel_->SetSizer(sizer);
  tabs_->AddPage(welcome_panel_, "Start", true);
  welcome_shown_ = true;
}

void ScriptEditorPanel::OnTabChanged(wxAuiNotebookEvent& evt) {
  active_tab_ = ActiveTab();
  status_message_.clear();
  ParseActiveIntoWorkspace();
  RebuildOutline();
  UpdateStatus();
  if (active_tab_) active_tab_->FocusEditor();
  evt.Skip();
}

void ScriptEditorPanel::OnTabClosing(wxAuiNotebookEvent& evt) {
  const int page = evt.GetSelection();
  auto* tab = dynamic_cast<ScriptEditorTab*>(tabs_->GetPage(page));
  if (!tab) {
    evt.Skip();
    return;
  }
  evt.Veto();
  TabCloseRequested(tab, false);
}

void ScriptEditorPanel::OnTabClosed(wxAuiNotebookEvent& evt) {
  active_tab_ = ActiveTab();
  ParseActiveIntoWorkspace();
  RebuildOutline();
  UpdateStatus();
  ShowWelcomeIfEmpty();
  evt.Skip();
}

ScriptEditorTab* ScriptEditorPanel::ActiveTab() const {
  const int page = tabs_->GetSelection();
  if (page == wxNOT_FOUND) return nullptr;
  return dynamic_cast<ScriptEditorTab*>(tabs_->GetPage(page));
}

std::vector<ScriptEditorTab*> ScriptEditorPanel::Tabs() const {
  std::vector<ScriptEditorTab*> result;
  for (size_t i = 0; i < tabs_->GetPageCount(); ++i) {
    if (auto* tab = dynamic_cast<ScriptEditorTab*>(tabs_->GetPage(i)))
      result.push_back(tab);
  }
  return result;
}

void ScriptEditorPanel::TabModified(ScriptEditorTab* tab) {
  if (!tab) return;
  SetTabTitle(tab);
}

void ScriptEditorPanel::TabCursorMoved(ScriptEditorTab* tab) {
  if (tab == active_tab_) UpdateStatus();
}

void ScriptEditorPanel::TabModeChanged(ScriptEditorTab* tab) {
  if (tab == active_tab_) UpdateStatus();
}

void ScriptEditorPanel::TabSaveRequested(ScriptEditorTab* tab) {
  if (tab) SaveActive();
}

bool ScriptEditorPanel::SaveActive() {
  ScriptEditorTab* tab = ActiveTab();
  if (!tab) return true;
  std::string error;
  if (!tab->Save(&error)) {
    wxMessageBox(wxString::FromUTF8(error), "Save Script", wxOK | wxICON_ERROR,
                 this);
    return false;
  }
  SetTabTitle(tab);
  workspace_.SetFile(tab->ResPath(),
                     tab->Editor()->GetText().utf8_str().data());
  SetStatusMessage("Saved " + wxString::FromUTF8(tab->ResPath()));
  return true;
}

bool ScriptEditorPanel::SaveAllDirty() {
  for (ScriptEditorTab* tab : Tabs()) {
    if (!tab->IsDirty()) continue;
    std::string error;
    if (!tab->Save(&error)) {
      wxMessageBox(wxString::FromUTF8(error), "Save Script",
                   wxOK | wxICON_ERROR, this);
      return false;
    }
    SetTabTitle(tab);
    workspace_.SetFile(tab->ResPath(),
                       tab->Editor()->GetText().utf8_str().data());
  }
  return true;
}

bool ScriptEditorPanel::HasDirty() const {
  for (ScriptEditorTab* tab : Tabs()) {
    if (tab->IsDirty()) return true;
  }
  return false;
}

bool ScriptEditorPanel::ConfirmCloseTab(ScriptEditorTab* tab) {
  if (!tab || !tab->IsDirty()) return true;
  const UnsavedChoice choice = ShowUnsavedChangesDialog(
      this, "Close " + wxString::FromUTF8(tab->ResPath()) + "?");
  if (choice == UnsavedChoice::kCancel) return false;
  if (choice == UnsavedChoice::kSave) {
    std::string error;
    if (!tab->Save(&error)) {
      wxMessageBox(wxString::FromUTF8(error), "Save Script",
                   wxOK | wxICON_ERROR, this);
      return false;
    }
  }
  return true;
}

bool ScriptEditorPanel::ConfirmCloseAll(const wxString& prompt) {
  if (!HasDirty()) return true;
  const UnsavedChoice choice = ShowUnsavedChangesDialog(
      this, prompt + "\nYou have unsaved script changes.");
  if (choice == UnsavedChoice::kCancel) return false;
  if (choice == UnsavedChoice::kSave) return SaveAllDirty();
  return true;
}

void ScriptEditorPanel::RemoveTab(ScriptEditorTab* tab) {
  if (!tab) return;
  const int page = tabs_->GetPageIndex(tab);
  if (page != wxNOT_FOUND) tabs_->DeletePage(page);
  auto it = tabs_by_path_.find(tab->ResPath());
  if (it != tabs_by_path_.end() && it->second == tab) tabs_by_path_.erase(it);
  if (active_tab_ == tab) active_tab_ = nullptr;
  active_tab_ = ActiveTab();
  ParseActiveIntoWorkspace();
  RebuildOutline();
  UpdateStatus();
  ShowWelcomeIfEmpty();
}

void ScriptEditorPanel::TabCloseRequested(ScriptEditorTab* tab, bool force) {
  if (!tab) return;
  if (!force && !ConfirmCloseTab(tab)) return;
  RemoveTab(tab);
}

void ScriptEditorPanel::CloseOtherTabs(ScriptEditorTab* tab) {
  for (ScriptEditorTab* other : Tabs()) {
    if (other == tab) continue;
    if (!ConfirmCloseTab(other)) continue;
    RemoveTab(other);
  }
}

void ScriptEditorPanel::RevealInProjectBrowser(ScriptEditorTab* tab) {
  if (!tab) return;
  scene_.SelectAsset(tab->ResPath());
}

void ScriptEditorPanel::CopyPathToClipboard(ScriptEditorTab* tab) {
  if (!tab) return;
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(
        new wxTextDataObject(wxString::FromUTF8(tab->ResPath())));
    wxTheClipboard->Close();
  }
}

wxString ScriptEditorPanel::TabTitle(ScriptEditorTab* tab) const {
  wxString title = wxString::FromUTF8(tab->Path().filename().string());
  if (tab->IsDirty()) title += " *";
  return title;
}

void ScriptEditorPanel::SetTabTitle(ScriptEditorTab* tab) {
  if (!tab) return;
  const int page = tabs_->GetPageIndex(tab);
  if (page == wxNOT_FOUND) return;
  tabs_->SetPageText(page, TabTitle(tab));
}

void ScriptEditorPanel::SyncWorkspaceFromTab(ScriptEditorTab* tab) {
  if (tab) {
    workspace_.SetFile(tab->ResPath(),
                       tab->Editor()->GetText().utf8_str().data());
  }
}

void ScriptEditorPanel::ParseActiveIntoWorkspace() {
  if (active_tab_) SyncWorkspaceFromTab(active_tab_);
}

std::string ScriptEditorPanel::RelativePath(
    const std::filesystem::path& file) const {
  auto project = scene_.GetProject();
  if (!project) return file.generic_string();
  std::error_code ec;
  const std::filesystem::path relative =
      std::filesystem::relative(file, project->GetRoot(), ec);
  return ec ? file.generic_string() : relative.generic_string();
}

std::filesystem::path ScriptEditorPanel::AbsolutePath(
    const std::string& res_path) const {
  auto project = scene_.GetProject();
  if (!project) return res_path;
  if (res_path.starts_with("res://")) return project->Resolve(res_path);
  return project->GetRoot() / res_path;
}

const std::vector<ScriptSymbol>& ScriptEditorPanel::NativeSymbols() const {
  return language_.NativeSymbols();
}

const std::string& ScriptEditorPanel::LanguageTypeKeywords() const {
  return language_.TypeKeywords();
}

void ScriptEditorPanel::RequestLint() {
  if (!scene_.GetProject()) return;
  lint_timer_.StartOnce(550);
}

void ScriptEditorPanel::OnLintTimer(wxTimerEvent&) {
  auto project = scene_.GetProject();
  if (!project) return;
  if (!language_.IsInitialized() && !language_.Initialize()) {
    SetStatusMessage(
        wxString::FromUTF8("Script language service: " + language_.Error()));
    return;
  }
  ParseActiveIntoWorkspace();
  std::map<std::string, std::string> open_buffers;
  for (ScriptEditorTab* tab : Tabs()) {
    open_buffers[tab->ResPath()] = tab->Editor()->GetText().utf8_str().data();
  }
  std::vector<std::pair<std::string, std::string>> sections;
  for (const auto& file : ScriptBuilder::ScriptFiles(project->GetRoot())) {
    const std::string res = RelativePath(file);
    auto it = open_buffers.find(res);
    sections.emplace_back(
        res, it != open_buffers.end() ? it->second : ReadText(file));
  }
  std::vector<ScriptDiagnostic> diagnostics;
  if (language_.Check(sections, &diagnostics)) {
    diagnostics_ = std::move(diagnostics);
    ApplyDiagnostics();
  }
  UpdateStatus();
}

void ScriptEditorPanel::ApplyDiagnostics() {
  for (ScriptEditorTab* tab : Tabs()) {
    tab->ApplyDiagnostics(diagnostics_);
  }
  RebuildProblems();
}

void ScriptEditorPanel::RebuildProblems() {
  if (!problems_) return;
  problems_->DeleteAllItems();
  problem_items_.clear();
  long row = 0;
  for (const ScriptDiagnostic& diagnostic : diagnostics_) {
    const wxString location =
        diagnostic.file.empty()
            ? "<engine>"
            : wxString::FromUTF8(diagnostic.file + ":" +
                                 std::to_string(diagnostic.line));
    problems_->InsertItem(row, location);
    problems_->SetItem(
        row, 1,
        wxString::Format("%s%s", diagnostic.warning ? "warning: " : "",
                         wxString::FromUTF8(diagnostic.message)));
    problem_items_.push_back({diagnostic.file.empty()
                                  ? std::filesystem::path{}
                                  : AbsolutePath(diagnostic.file),
                              diagnostic.line});
    ++row;
  }
}

void ScriptEditorPanel::OnProblemActivated(wxListEvent& evt) {
  const long index = evt.GetIndex();
  if (index < 0 || static_cast<size_t>(index) >= problem_items_.size()) return;
  const ProblemItem& item = problem_items_[index];
  if (!item.file.empty()) OpenFile(item.file, item.line);
}

void ScriptEditorPanel::RebuildOutline() {
  outline_->DeleteAllItems();
  const wxTreeItemId root = outline_->AddRoot("Outline");
  if (!active_tab_) return;
  const ParsedScript parsed =
      ParseScript(active_tab_->ResPath(),
                  active_tab_->Editor()->GetText().utf8_str().data());
  std::map<std::string, wxTreeItemId> classes;
  for (const ScriptSymbol& symbol : parsed.symbols) {
    if (symbol.kind == ScriptSymbolKind::kClass ||
        symbol.kind == ScriptSymbolKind::kEnum ||
        symbol.kind == ScriptSymbolKind::kNamespace) {
      const wxString label = symbol.signature.empty()
                                 ? wxString::FromUTF8(symbol.name)
                                 : wxString::FromUTF8(symbol.signature);
      classes[symbol.name] = outline_->AppendItem(root, label, -1, -1,
                                                  new OutlineItem(symbol.line));
    }
  }
  for (const ScriptSymbol& symbol : parsed.symbols) {
    if (symbol.kind == ScriptSymbolKind::kClass ||
        symbol.kind == ScriptSymbolKind::kEnum ||
        symbol.kind == ScriptSymbolKind::kNamespace) {
      continue;
    }
    wxTreeItemId parent = root;
    auto it = classes.find(symbol.owner);
    if (it != classes.end()) parent = it->second;
    wxString label = symbol.signature.empty()
                         ? wxString::FromUTF8(symbol.name)
                         : wxString::FromUTF8(symbol.signature);
    if (symbol.kind == ScriptSymbolKind::kField) label += "  [field]";
    if (symbol.kind == ScriptSymbolKind::kEnumValue) label = "  " + label;
    outline_->AppendItem(parent, label, -1, -1, new OutlineItem(symbol.line));
  }
  outline_->ExpandAll();
}

void ScriptEditorPanel::OnOutlineActivated(wxTreeEvent& evt) {
  auto* item = dynamic_cast<OutlineItem*>(outline_->GetItemData(evt.GetItem()));
  if (!item || !active_tab_) return;
  active_tab_->GoTo(item->line);
  evt.Skip();
}

void ScriptEditorPanel::ShowGoToMethod() {
  ParseActiveIntoWorkspace();
  std::vector<ScriptSymbol> symbols;
  for (const ParsedScript& file : workspace_.Files()) {
    for (const ScriptSymbol& symbol : file.symbols) {
      if (symbol.file.empty()) continue;
      if (symbol.kind == ScriptSymbolKind::kMethod ||
          symbol.kind == ScriptSymbolKind::kFunction ||
          symbol.kind == ScriptSymbolKind::kFuncdef) {
        symbols.push_back(symbol);
      }
    }
  }
  if (symbols.empty()) {
    SetStatusMessage("No methods or functions found");
    return;
  }
  SymbolPickerDialog dialog(this, std::move(symbols));
  if (dialog.ShowModal() == wxID_OK && !dialog.SelectedFile().empty()) {
    OpenLocation(dialog.SelectedFile(), dialog.SelectedLine());
  }
}

bool ScriptEditorPanel::IsEditorFocused() const {
  wxWindow* focus = wxWindow::FindFocus();
  return focus && dynamic_cast<wxStyledTextCtrl*>(focus) != nullptr &&
         IsDescendantOf(focus, this);
}

void ScriptEditorPanel::ApplySettings() {
  for (ScriptEditorTab* tab : Tabs()) {
    tab->SetVimEnabled(scene_.Settings().vim_mode);
  }
}

void ScriptEditorPanel::UpdateStatus() {
  if (!status_) return;
  if (!active_tab_) {
    status_->SetLabel("No script open");
    return;
  }
  const int problem_count = static_cast<int>(diagnostics_.size());
  wxString text = wxString::Format(
      "%s   Ln %d, Col %d   %d problem%s", active_tab_->ModeName(),
      active_tab_->CurrentLine(), active_tab_->CurrentColumn(), problem_count,
      problem_count == 1 ? "" : "s");
  if (!status_message_.empty()) {
    text += "   |   ";
    text += status_message_;
  }
  status_->SetLabel(text);
}

void ScriptEditorPanel::SetStatusMessage(const wxString& text) {
  status_message_ = text;
  UpdateStatus();
}

void ScriptEditorPanel::ShowCommandLine(
    const wxString& prefix, std::function<void(const wxString&)> on_submit) {
  command_line_prefix_ = prefix;
  command_line_submit_ = std::move(on_submit);
  command_line_->SetValue(prefix);
  command_line_->SetInsertionPointEnd();
  command_line_->Show();
  Layout();
  command_line_->SetFocus();
}

void ScriptEditorPanel::HideCommandLine() {
  command_line_submit_ = nullptr;
  command_line_prefix_.clear();
  command_line_->Hide();
  Layout();
  if (active_tab_) active_tab_->FocusEditor();
}

void ScriptEditorPanel::ShowFindBar(bool replace) {
  find_replace_mode_ = replace;
  find_bar_->Show();
  replace_text_->Show(replace);
  replace_button_->Show(replace);
  replace_all_button_->Show(replace);
  if (!find_text_->GetValue().empty()) {
    // Keep the last search.
  } else if (active_tab_) {
    const wxString selected = active_tab_->Editor()->GetSelectedText();
    if (!selected.empty() && selected.Find('\n') == wxNOT_FOUND)
      find_text_->SetValue(selected);
  }
  find_bar_->Layout();
  Layout();
  find_text_->SetFocus();
  find_text_->SelectAll();
}

void ScriptEditorPanel::DoFind(const wxString& text, bool backward,
                               bool select_only) {
  if (!active_tab_ || text.empty()) return;
  wxStyledTextCtrl* editor = active_tab_->Editor();
  int flags = 0;
  if (find_case_->GetValue()) flags |= wxSTC_FIND_MATCHCASE;
  if (find_regex_->GetValue()) flags |= wxSTC_FIND_REGEXP;
  editor->SetSearchFlags(flags);

  const int length = editor->GetTextLength();
  int from = 0;
  int to = length;
  if (backward) {
    to = std::max(0, editor->GetSelectionStart() - 1);
  } else {
    from = std::min(length, editor->GetSelectionEnd() + 1);
  }
  auto search = [&](int start, int end) {
    editor->SetTargetStart(start);
    editor->SetTargetEnd(end);
    return editor->SearchInTarget(text);
  };
  int found = search(from, to);
  if (found < 0) {
    found = backward ? search(0, to) : search(0, length);
  }
  if (found < 0) {
    SetStatusMessage("Not found: " + text);
    return;
  }
  const int match_end = editor->GetTargetEnd();
  editor->SetSelection(found, match_end);
  editor->EnsureCaretVisible();
  editor->SetFocus();
  if (select_only) {
    // Left for callers that only want a highlight.
  }
}

void ScriptEditorPanel::FindNext(bool backward) {
  DoFind(find_text_->GetValue(), backward, false);
}

}  // namespace Mercury::EditorUI
