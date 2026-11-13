/*
 * script_editor_tab.h
 *
 * One open script buffer: a themed wxStyledTextCtrl plus completion, calltips,
 * diagnostics markers and optional Vim mode.
 */
#ifndef MERCURY_EDITOR_SCRIPT_EDITOR_TAB_H
#define MERCURY_EDITOR_SCRIPT_EDITOR_TAB_H

#include <wx/stc/stc.h>
#include <wx/panel.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "script_language.h"
#include "script_vim.h"

namespace Mercury::EditorUI {

class ScriptEditorPanel;

class ScriptEditorTab : public wxPanel {
 public:
  ScriptEditorTab(wxWindow* parent, ScriptEditorPanel* panel,
                  std::filesystem::path file, std::string res_path);
  ~ScriptEditorTab() override = default;

  bool Load(std::string* error);
  bool Save(std::string* error);
  bool IsDirty() const;
  bool ReloadFromDisk(std::string* error);

  const std::filesystem::path& Path() const { return file_; }
  const std::string& ResPath() const { return res_path_; }
  wxStyledTextCtrl* Editor() { return editor_; }
  void FocusEditor();
  void GoTo(int line, int column = 1);
  int CurrentLine() const;
  int CurrentColumn() const;

  void SetVimEnabled(bool enabled);
  bool VimEnabled() const;
  const char* ModeName() const;

  void ApplyTheme();
  void ApplyTypeKeywords();
  void ApplyDiagnostics(const std::vector<ScriptDiagnostic>& diagnostics);
  void ClearDiagnostics();
  void UpdateLineNumberMargin();
  void RefreshIndicators();

  void ShowCompletion(bool force);
  void ShowCallTip();

 private:
  void ApplyThemeStyles();
  void BindEvents();
  bool IsInCode(int pos) const;
  std::string CurrentWord(int* prefix_length) const;
  void OnCharAdded(wxStyledTextEvent& evt);
  void OnModified(wxStyledTextEvent& evt);
  void OnUpdateUI(wxStyledTextEvent& evt);
  void OnMarginClick(wxStyledTextEvent& evt);
  void OnAutoCompSelection(wxStyledTextEvent& evt);
  void OnEditorKeyDown(wxKeyEvent& evt);
  void OnMouseDown(wxMouseEvent& evt);
  void AutoIndent(int pos);
  void GoToDefinition(int pos);

  ScriptEditorPanel* panel_ = nullptr;
  std::filesystem::path file_;
  std::string res_path_;
  std::string eol_;
  wxStyledTextCtrl* editor_ = nullptr;
  std::unique_ptr<ScriptVim> vim_;
  bool vim_enabled_ = false;
  std::vector<ScriptDiagnostic> diagnostics_;
  std::map<std::string, std::string> completion_details_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCRIPT_EDITOR_TAB_H
