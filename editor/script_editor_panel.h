/*
 * script_editor_panel.h
 *
 * Godot-style built-in code editor: project script list, open script tabs,
 * outline/problems sidebar, status line and a small find bar.
 */
#ifndef MERCURY_EDITOR_SCRIPT_EDITOR_PANEL_H
#define MERCURY_EDITOR_SCRIPT_EDITOR_PANEL_H

#include <wx/aui/auibook.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/splitter.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/treectrl.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "script_language.h"

namespace Mercury {
class EditorScene;
}

namespace Mercury::EditorUI {

class ScriptEditorTab;

class ScriptEditorPanel : public wxPanel {
 public:
  ScriptEditorPanel(wxWindow* parent, Mercury::EditorScene& scene);
  ~ScriptEditorPanel() override;

  void OpenFile(const std::filesystem::path& file, int line = 0);
  void OpenLocation(const std::string& res_path, int line);
  bool SaveActive();
  bool SaveAllDirty();
  bool HasDirty() const;
  bool ConfirmCloseAll(const wxString& prompt);
  bool IsEditorFocused() const;
  void ApplySettings();
  void RefreshScriptList();
  void Sync();
  void ShowGoToMethod();

  void RequestLint();
  void ApplyDiagnostics();

  // Editor tab callbacks.
  void TabModified(ScriptEditorTab* tab);
  void TabCursorMoved(ScriptEditorTab* tab);
  void TabModeChanged(ScriptEditorTab* tab);
  void TabSaveRequested(ScriptEditorTab* tab);
  void TabCloseRequested(ScriptEditorTab* tab, bool force);
  void UpdateStatus();
  void SetStatusMessage(const wxString& text);
  void SetTabTitle(ScriptEditorTab* tab);

  // Used by ScriptEditorTab.
  Mercury::EditorScene& Scene() { return scene_; }
  ScriptWorkspace& Workspace() { return workspace_; }
  const std::vector<ScriptSymbol>& NativeSymbols() const;
  const std::string& LanguageTypeKeywords() const;
  std::string RelativePath(const std::filesystem::path& file) const;
  std::filesystem::path AbsolutePath(const std::string& res_path) const;
  void ShowCommandLine(const wxString& prefix,
                       std::function<void(const wxString&)> on_submit);
  void HideCommandLine();

  void ShowFindBar(bool replace);
  void FindNext(bool backward);

  ScriptEditorTab* ActiveTab() const;
  std::vector<ScriptEditorTab*> Tabs() const;

 private:
  struct ScriptTreeItem : public wxTreeItemData {
    std::filesystem::path path;
    explicit ScriptTreeItem(std::filesystem::path p) : path(std::move(p)) {}
  };
  struct OutlineItem : public wxTreeItemData {
    int line = 0;
    explicit OutlineItem(int l) : line(l) {}
  };
  struct ProblemItem {
    std::filesystem::path file;
    int line = 0;
  };

  void BuildUi();
  void RebuildScriptTree();
  void RebuildOutline();
  void RebuildProblems();
  void SelectTab(ScriptEditorTab* tab);
  void RemoveTab(ScriptEditorTab* tab);
  void ShowWelcomeIfEmpty();
  void SyncWorkspaceFromTab(ScriptEditorTab* tab);
  void ParseActiveIntoWorkspace();
  void OnLintTimer(wxTimerEvent& evt);
  void OnScriptActivated(wxTreeEvent& evt);
  void OnOutlineActivated(wxTreeEvent& evt);
  void OnProblemActivated(wxListEvent& evt);
  void OnTabChanged(wxAuiNotebookEvent& evt);
  void OnTabClosing(wxAuiNotebookEvent& evt);
  void OnTabClosed(wxAuiNotebookEvent& evt);
  void DoFind(const wxString& text, bool backward, bool select_only);
  bool ConfirmCloseTab(ScriptEditorTab* tab);
  void CloseOtherTabs(ScriptEditorTab* tab);
  void RevealInProjectBrowser(ScriptEditorTab* tab);
  void CopyPathToClipboard(ScriptEditorTab* tab);
  wxString TabTitle(ScriptEditorTab* tab) const;

  Mercury::EditorScene& scene_;
  ScriptLanguageService language_;
  ScriptWorkspace workspace_;

  wxAuiNotebook* tabs_ = nullptr;
  wxSearchCtrl* script_filter_ = nullptr;
  wxTreeCtrl* script_tree_ = nullptr;
  wxTreeCtrl* outline_ = nullptr;
  wxListCtrl* problems_ = nullptr;
  wxStaticText* status_ = nullptr;
  wxTextCtrl* command_line_ = nullptr;
  wxPanel* find_bar_ = nullptr;
  wxSearchCtrl* find_text_ = nullptr;
  wxTextCtrl* replace_text_ = nullptr;
  wxButton* replace_button_ = nullptr;
  wxButton* replace_all_button_ = nullptr;
  wxCheckBox* find_case_ = nullptr;
  wxCheckBox* find_regex_ = nullptr;

  wxPanel* welcome_panel_ = nullptr;
  bool welcome_shown_ = false;

  wxTimer lint_timer_;
  std::vector<ProblemItem> problem_items_;
  std::vector<ScriptDiagnostic> diagnostics_;
  wxString status_message_;
  bool find_replace_mode_ = false;
  std::function<void(const wxString&)> command_line_submit_;
  wxString command_line_prefix_;
  std::map<std::string, ScriptEditorTab*> tabs_by_path_;
  ScriptEditorTab* active_tab_ = nullptr;
  size_t last_project_files_revision_ = 0;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCRIPT_EDITOR_PANEL_H
