/*
 * script_vim.h
 *
 * Modal (Vim-style) key handling for a wxStyledTextCtrl. This is a focused
 * layer, not a full Vim implementation: normal/insert/replace/visual modes,
 * motions, operators, text objects, search and basic ex commands.
 */
#ifndef MERCURY_EDITOR_SCRIPT_VIM_H
#define MERCURY_EDITOR_SCRIPT_VIM_H

#include <wx/stc/stc.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Mercury::EditorUI {

class ScriptVim {
 public:
  enum class Mode { kNormal, kInsert, kReplace, kVisual, kVisualLine };

  struct Callbacks {
    std::function<bool()> save;
    std::function<bool(bool force)> close;
    std::function<void(const wxString& prefix,
                       std::function<void(const wxString&)> on_submit)>
        prompt;
    std::function<void()> status_changed;
    std::function<void(const wxString& message)> message;
  };

  ScriptVim(wxStyledTextCtrl* editor, Callbacks callbacks);

  void SetEnabled(bool enabled);
  bool IsEnabled() const { return enabled_; }
  Mode GetMode() const { return mode_; }
  const char* ModeName() const;
  void Reset();

  // Returns true when the key was consumed. Insert mode passes ordinary keys
  // through so Scintilla keeps handling text input natively.
  bool OnKeyDown(wxKeyEvent& evt);

 private:
  struct Motion {
    std::size_t pos = 0;
    bool valid = false;
    bool linewise = false;
    bool inclusive = false;
  };

  void SetMode(Mode mode);
  void EnterInsert(std::size_t pos);
  void EnterInsertForChange(std::size_t pos);
  void LeaveInsert();
  bool HandleInsertKey(wxKeyEvent& evt);
  bool HandleNormalKey(wxKeyEvent& evt);
  bool HandleVisualKey(wxKeyEvent& evt);

  int TakeCount();
  void ClearPending();
  void ApplyOperator(char op, const Motion& motion);
  void ApplyOperatorRange(char op, std::size_t from, std::size_t to,
                          bool linewise);
  void ApplyTextObject(char object, bool inner);
  Motion MotionFor(int key, int count, bool for_operator);
  Motion FindMotion(char find_command, int count, int character);
  Motion ParagraphMotion(bool forward, int count);

  std::string Text() const;
  std::size_t Pos() const;
  void SetPos(std::size_t pos);
  std::size_t LineStart(std::size_t pos) const;
  std::size_t LineEnd(std::size_t pos) const;
  std::size_t FirstNonBlank(std::size_t pos) const;
  std::size_t Clamp(std::size_t pos) const;
  void UpdateVisualSelection();
  void MoveToLine(int line);
  std::size_t WordUnderCursor() const;
  void Search(const std::string& pattern, bool backward, int count);
  void SearchWord(bool backward, int count);
  void ExecuteEx(const wxString& command);
  void YankRange(std::size_t from, std::size_t to, bool linewise);
  void Put(bool after, int count);
  void DeleteChar(int count, bool backward);
  void ReplaceChar(int count, int character);
  void ToggleCase(int count);
  void JoinLines(int count);
  void ShiftLines(int count, bool right);
  void OpenLine(bool below);
  void ChangeToLineEnd();
  void DeleteToLineEnd();
  void YankLine(int count);
  void IndentCurrentLines(int count, bool right);
  void ScrollLines(int lines);
  void Message(const wxString& text);

  wxStyledTextCtrl* editor_ = nullptr;
  Callbacks callbacks_;
  bool enabled_ = false;
  Mode mode_ = Mode::kNormal;

  int count_ = 0;
  bool has_count_ = false;
  char pending_operator_ = 0;
  char pending_g_ = 0;
  char pending_g_operator_ = 0;
  char pending_find_ = 0;
  char pending_replace_ = 0;
  char pending_object_ = 0;

  std::size_t visual_anchor_ = 0;
  std::size_t insert_start_ = 0;
  std::string yank_;
  bool yank_linewise_ = false;
  std::string last_search_;
  bool last_search_backward_ = false;
  char last_find_command_ = 0;
  int last_find_char_ = 0;
  std::function<void()> last_change_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCRIPT_VIM_H
