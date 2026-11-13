/*
 * script_vim.cc
 */
#include "script_vim.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Mercury::EditorUI {

namespace {

bool IsWordChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsSpace(char c) { return std::isspace(static_cast<unsigned char>(c)); }

size_t NextWordStart(const std::string& text, size_t pos, bool big, int count) {
  while (count-- > 0) {
    if (pos >= text.size()) break;
    auto in_word = [&](char c) { return big ? !IsSpace(c) : IsWordChar(c); };
    if (in_word(text[pos])) {
      while (pos < text.size() && in_word(text[pos])) ++pos;
    } else if (!IsSpace(text[pos])) {
      while (pos < text.size() && !in_word(text[pos]) && !IsSpace(text[pos]))
        ++pos;
    }
    while (pos < text.size() && IsSpace(text[pos])) ++pos;
  }
  return pos;
}

size_t WordEnd(const std::string& text, size_t pos, bool big, int count) {
  while (count-- > 0) {
    if (pos >= text.size()) break;
    auto in_word = [&](char c) { return big ? !IsSpace(c) : IsWordChar(c); };
    while (pos < text.size() && IsSpace(text[pos])) ++pos;
    if (pos >= text.size()) break;
    while (pos + 1 < text.size() && in_word(text[pos]) &&
           in_word(text[pos + 1]))
      ++pos;
    if (count > 0) ++pos;
  }
  return pos;
}

size_t PrevWordStart(const std::string& text, size_t pos, bool big, int count) {
  while (count-- > 0) {
    if (pos > 0) --pos;
    while (pos > 0 && IsSpace(text[pos])) --pos;
    auto in_word = [&](char c) { return big ? !IsSpace(c) : IsWordChar(c); };
    while (pos > 0 && in_word(text[pos - 1]) == in_word(text[pos]) &&
           !IsSpace(text[pos - 1]))
      --pos;
  }
  return pos;
}

}  // namespace

ScriptVim::ScriptVim(wxStyledTextCtrl* editor, Callbacks callbacks)
    : editor_(editor), callbacks_(std::move(callbacks)) {}

const char* ScriptVim::ModeName() const {
  switch (mode_) {
    case Mode::kInsert:
      return "INSERT";
    case Mode::kReplace:
      return "REPLACE";
    case Mode::kVisual:
      return "VISUAL";
    case Mode::kVisualLine:
      return "V-LINE";
    case Mode::kNormal:
    default:
      return "NORMAL";
  }
}

void ScriptVim::SetEnabled(bool enabled) {
  if (enabled_ == enabled) return;
  enabled_ = enabled;
  if (enabled_) {
    ClearPending();
    SetMode(Mode::kNormal);
  } else {
    ClearPending();
    SetMode(Mode::kInsert);
  }
}

void ScriptVim::Reset() {
  ClearPending();
  if (enabled_) SetMode(Mode::kNormal);
}

void ScriptVim::SetMode(Mode mode) {
  mode_ = mode;
  if (mode == Mode::kInsert || mode == Mode::kReplace) {
    editor_->SetCaretStyle(wxSTC_CARETSTYLE_LINE);
    editor_->SetCaretWidth(2);
    editor_->SetOvertype(mode == Mode::kReplace);
  } else {
    editor_->SetCaretStyle(wxSTC_CARETSTYLE_BLOCK);
    editor_->SetCaretWidth(2);
    editor_->SetOvertype(false);
  }
  if (callbacks_.status_changed) callbacks_.status_changed();
}

int ScriptVim::TakeCount() {
  const int value = count_ > 0 ? count_ : 1;
  count_ = 0;
  has_count_ = false;
  return value;
}

void ScriptVim::ClearPending() {
  pending_operator_ = 0;
  pending_g_ = 0;
  pending_g_operator_ = 0;
  pending_find_ = 0;
  pending_replace_ = 0;
  pending_object_ = 0;
}

std::string ScriptVim::Text() const {
  const wxCharBuffer buffer = editor_->GetTextRaw();
  return std::string(buffer.data(), buffer.length());
}

std::size_t ScriptVim::Pos() const {
  return static_cast<std::size_t>(editor_->GetCurrentPos());
}

std::size_t ScriptVim::Clamp(std::size_t pos) const {
  const size_t length = static_cast<size_t>(editor_->GetTextLength());
  return std::min(pos, length);
}

void ScriptVim::SetPos(std::size_t pos) {
  pos = Clamp(pos);
  editor_->SetSelection(static_cast<int>(pos), static_cast<int>(pos));
  editor_->EnsureCaretVisible();
}

std::size_t ScriptVim::LineStart(std::size_t pos) const {
  return static_cast<size_t>(editor_->PositionFromLine(
      editor_->LineFromPosition(static_cast<int>(Clamp(pos)))));
}

std::size_t ScriptVim::LineEnd(std::size_t pos) const {
  const int line = editor_->LineFromPosition(static_cast<int>(Clamp(pos)));
  return static_cast<size_t>(editor_->GetLineEndPosition(line));
}

std::size_t ScriptVim::FirstNonBlank(std::size_t pos) const {
  size_t at = LineStart(pos);
  const size_t end = LineEnd(pos);
  const std::string text = Text();
  while (at < end && (text[at] == ' ' || text[at] == '\t')) ++at;
  return at;
}

std::size_t ScriptVim::WordUnderCursor() const {
  const std::string text = Text();
  const size_t pos = Clamp(Pos());
  if (text.empty()) return 0;
  size_t at = std::min(pos, text.size() - 1);
  if (IsSpace(text[at]) || !IsWordChar(text[at])) return 0;
  return at;
}

void ScriptVim::Message(const wxString& text) {
  if (callbacks_.message) callbacks_.message(text);
}

void ScriptVim::EnterInsert(std::size_t pos) {
  SetPos(pos);
  insert_start_ = Pos();
  SetMode(Mode::kInsert);
}

void ScriptVim::EnterInsertForChange(std::size_t pos) {
  SetPos(pos);
  insert_start_ = Pos();
  SetMode(Mode::kInsert);
}

void ScriptVim::LeaveInsert() {
  std::size_t pos = Pos();
  if (pos > 0) {
    const int line = editor_->LineFromPosition(static_cast<int>(pos));
    const int prev_line = editor_->LineFromPosition(static_cast<int>(pos - 1));
    if (line == prev_line) --pos;
  }
  SetPos(pos);
  SetMode(Mode::kNormal);
}

bool ScriptVim::OnKeyDown(wxKeyEvent& evt) {
  if (!enabled_) return false;
  const bool command = evt.CmdDown();
  if (command) return false;  // leave Cmd+C/V/S to the application
  if (mode_ == Mode::kInsert || mode_ == Mode::kReplace) {
    return HandleInsertKey(evt);
  }
  if (evt.ControlDown() && evt.GetKeyCode() == 'F') return false;
  if (evt.ControlDown() && evt.GetKeyCode() == 'H') return false;
  if (evt.ControlDown() && evt.GetKeyCode() == WXK_SPACE) return false;
  if (mode_ == Mode::kVisual || mode_ == Mode::kVisualLine) {
    return HandleVisualKey(evt);
  }
  return HandleNormalKey(evt);
}

bool ScriptVim::HandleInsertKey(wxKeyEvent& evt) {
  const int key = evt.GetKeyCode();
  const bool ctrl = evt.ControlDown();
  if (key == WXK_ESCAPE || (ctrl && key == '[')) {
    if (editor_->AutoCompActive()) {
      editor_->AutoCompCancel();
      return true;
    }
    if (editor_->CallTipActive()) {
      editor_->CallTipCancel();
      return true;
    }
    LeaveInsert();
    return true;
  }
  if (ctrl && (key == 'S' || key == 's')) {
    if (callbacks_.save) callbacks_.save();
    return true;
  }
  if (ctrl && (key == 'W' || key == 'w')) {
    if (callbacks_.close) callbacks_.close(false);
    return true;
  }
  return false;
}

ScriptVim::Motion ScriptVim::MotionFor(int key, int count, bool for_operator) {
  Motion motion;
  const std::string text = Text();
  const size_t pos = Clamp(Pos());
  const int line = editor_->LineFromPosition(static_cast<int>(pos));

  switch (key) {
    case 'h':
    case WXK_LEFT:
      motion.pos = pos >= static_cast<size_t>(count)
                       ? pos - static_cast<size_t>(count)
                       : 0;
      motion.pos = std::max(motion.pos, LineStart(pos));
      motion.valid = true;
      break;
    case 'l':
    case WXK_RIGHT:
      motion.pos = std::min(LineEnd(pos), pos + static_cast<size_t>(count));
      motion.valid = true;
      break;
    case 'j':
    case WXK_DOWN: {
      const int target = std::min(line + count, editor_->GetLineCount() - 1);
      const int column = editor_->GetColumn(static_cast<int>(pos));
      const int at = editor_->FindColumn(target, column);
      motion.pos = static_cast<size_t>(
          std::min(at, editor_->GetLineEndPosition(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'k':
    case WXK_UP: {
      const int target = std::max(0, line - count);
      const int column = editor_->GetColumn(static_cast<int>(pos));
      const int at = editor_->FindColumn(target, column);
      motion.pos = static_cast<size_t>(
          std::min(at, editor_->GetLineEndPosition(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'w':
      motion.pos = NextWordStart(text, pos, false, count);
      motion.valid = true;
      break;
    case 'W':
      motion.pos = NextWordStart(text, pos, true, count);
      motion.valid = true;
      break;
    case 'b':
      motion.pos = PrevWordStart(text, pos, false, count);
      motion.valid = true;
      break;
    case 'B':
      motion.pos = PrevWordStart(text, pos, true, count);
      motion.valid = true;
      break;
    case 'e':
      motion.pos = WordEnd(text, pos, false, count);
      motion.inclusive = true;
      motion.valid = true;
      break;
    case 'E':
      motion.pos = WordEnd(text, pos, true, count);
      motion.inclusive = true;
      motion.valid = true;
      break;
    case '0':
    case WXK_HOME:
      motion.pos = LineStart(pos);
      motion.valid = true;
      break;
    case '^': {
      motion.pos = FirstNonBlank(pos);
      motion.valid = true;
      break;
    }
    case '$':
    case WXK_END:
      motion.pos = LineEnd(pos);
      if (!for_operator && motion.pos > LineStart(pos)) --motion.pos;
      motion.valid = true;
      break;
    case WXK_RETURN:
    case '+': {
      const int target = std::min(line + count, editor_->GetLineCount() - 1);
      motion.pos =
          FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case '-': {
      const int target = std::max(0, line - count);
      motion.pos =
          FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case '%': {
      const int found = editor_->BraceMatch(static_cast<int>(pos));
      if (found >= 0) {
        motion.pos = static_cast<size_t>(found);
        motion.inclusive = true;
        motion.valid = true;
      }
      break;
    }
    case '{':
    case '}':
      motion = ParagraphMotion(key == '}', count);
      break;
    case 'G': {
      int target = editor_->GetLineCount() - 1;
      if (count > 1) target = std::min(count - 1, target);
      motion.pos =
          FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'g': {
      motion.pos = static_cast<size_t>(editor_->PositionFromLine(0));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'H': {
      const int target = editor_->GetFirstVisibleLine();
      motion.pos =
          FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'M': {
      const int target = editor_->GetFirstVisibleLine() +
                         std::max(0, editor_->LinesOnScreen() / 2);
      motion.pos = FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(
          std::min(target, editor_->GetLineCount() - 1))));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    case 'L': {
      const int target = std::min(editor_->GetFirstVisibleLine() +
                                      std::max(0, editor_->LinesOnScreen() - 1),
                                  editor_->GetLineCount() - 1);
      motion.pos =
          FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(target)));
      motion.linewise = true;
      motion.valid = true;
      break;
    }
    default:
      break;
  }
  return motion;
}

ScriptVim::Motion ScriptVim::ParagraphMotion(bool forward, int count) {
  Motion motion;
  const std::string text = Text();
  size_t pos = Clamp(Pos());
  for (int i = 0; i < count; ++i) {
    if (forward) {
      pos = LineEnd(pos);
      if (pos < text.size()) ++pos;
      while (pos < text.size()) {
        const size_t end = static_cast<size_t>(editor_->GetLineEndPosition(
            editor_->LineFromPosition(static_cast<int>(pos))));
        bool blank = true;
        for (size_t k = pos; k < end; ++k) {
          if (!IsSpace(text[k])) {
            blank = false;
            break;
          }
        }
        if (blank) break;
        pos = end;
        if (pos < text.size()) ++pos;
      }
    } else {
      if (pos > 0) --pos;
      pos = LineStart(pos);
      while (pos > 0) {
        const size_t previous = LineStart(pos - 1);
        const size_t end = LineEnd(previous);
        bool blank = true;
        for (size_t k = previous; k < end; ++k) {
          if (!IsSpace(text[k])) {
            blank = false;
            break;
          }
        }
        if (blank) {
          pos = previous;
          break;
        }
        pos = previous;
      }
    }
  }
  motion.pos = pos;
  motion.valid = true;
  return motion;
}

ScriptVim::Motion ScriptVim::FindMotion(char find_command, int count,
                                        int character) {
  Motion motion;
  const std::string text = Text();
  const size_t pos = Clamp(Pos());
  const size_t begin = LineStart(pos);
  const size_t end = LineEnd(pos);
  auto matches = [&](size_t at) {
    return at < text.size() && static_cast<unsigned char>(text[at]) ==
                                   static_cast<unsigned char>(character);
  };
  if (find_command == 't' || find_command == 'T') {
    // `t`/`T` stop one character before the target.
    if (find_command == 't') {
      size_t at = pos;
      while (count-- > 0) {
        at = begin;
        size_t found = std::string::npos;
        for (size_t k = pos + 1; k < end; ++k) {
          if (matches(k)) {
            found = k;
            break;
          }
        }
        if (found == std::string::npos) return motion;
        at = found;
        if (count > 0) {
          // Find the next occurrence after this one.
          size_t next = std::string::npos;
          for (size_t k = found + 1; k < end; ++k) {
            if (matches(k)) {
              next = k;
              break;
            }
          }
          if (next == std::string::npos) return motion;
          motion.pos = next - 1;
          motion.valid = true;
          continue;
        }
        motion.pos = at > begin ? at - 1 : at;
        motion.valid = true;
      }
      return motion;
    }
    size_t at = pos;
    while (count-- > 0) {
      size_t found = std::string::npos;
      for (size_t k = at; k > begin; --k) {
        if (matches(k - 1)) {
          found = k - 1;
          break;
        }
      }
      if (found == std::string::npos) return motion;
      at = found;
      motion.pos = std::min(end, at + 1);
      motion.valid = true;
    }
    return motion;
  }

  const bool backward = find_command == 'F';
  size_t at = pos;
  while (count-- > 0) {
    size_t found = std::string::npos;
    if (backward) {
      for (size_t k = at; k > begin; --k) {
        if (matches(k - 1)) {
          found = k - 1;
          break;
        }
      }
      if (found == std::string::npos) return motion;
      at = found;
      motion.pos = at;
    } else {
      for (size_t k = at + 1; k < end; ++k) {
        if (matches(k)) {
          found = k;
          break;
        }
      }
      if (found == std::string::npos) return motion;
      at = found;
      motion.pos = at;
    }
    motion.valid = true;
    motion.inclusive = true;
  }
  return motion;
}

void ScriptVim::UpdateVisualSelection() {
  if (!(mode_ == Mode::kVisual || mode_ == Mode::kVisualLine)) return;
  int from = static_cast<int>(std::min(visual_anchor_, Pos()));
  int to = static_cast<int>(std::max(visual_anchor_, Pos()));
  if (mode_ == Mode::kVisualLine) {
    from = static_cast<int>(LineStart(static_cast<size_t>(from)));
    const int line = editor_->LineFromPosition(to);
    to = static_cast<int>(LineEnd(static_cast<size_t>(to)));
    if (editor_->GetLineEndPosition(line) < editor_->GetTextLength()) ++to;
  } else {
    if (to < editor_->GetTextLength()) ++to;
  }
  editor_->SetSelection(from, to);
}

void ScriptVim::MoveToLine(int line) {
  line = std::max(0, std::min(line, editor_->GetLineCount() - 1));
  SetPos(FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(line))));
}

void ScriptVim::YankRange(std::size_t from, std::size_t to, bool linewise) {
  if (to <= from) return;
  yank_ = Text().substr(from, to - from);
  yank_linewise_ = linewise;
}

void ScriptVim::ApplyOperatorRange(char op, std::size_t from, std::size_t to,
                                   bool linewise) {
  if (to < from) std::swap(from, to);
  if (op == 'y') {
    YankRange(from, to, linewise);
    return;
  }
  if (op == '>' || op == '<') {
    const int first = editor_->LineFromPosition(static_cast<int>(from));
    const int last = editor_->LineFromPosition(static_cast<int>(to));
    editor_->SetCurrentPos(editor_->PositionFromLine(first));
    IndentCurrentLines(std::max(1, last - first + 1), op == '>');
    return;
  }
  if (op == '~' || op == 'u' || op == 'U') {
    std::string text = Text();
    if (to > text.size()) to = text.size();
    std::string slice = text.substr(from, to - from);
    for (char& c : slice) {
      if (op == '~') {
        if (std::islower(static_cast<unsigned char>(c)))
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else if (std::isupper(static_cast<unsigned char>(c)))
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      } else if (op == 'u') {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      } else {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      }
    }
    editor_->BeginUndoAction();
    editor_->SetTargetStart(static_cast<int>(from));
    editor_->SetTargetEnd(static_cast<int>(to));
    editor_->ReplaceTarget(wxString::FromUTF8(slice));
    editor_->EndUndoAction();
    SetPos(from);
    return;
  }
  YankRange(from, to, linewise);
  editor_->BeginUndoAction();
  editor_->SetTargetStart(static_cast<int>(from));
  editor_->SetTargetEnd(static_cast<int>(to));
  editor_->ReplaceTarget("");
  editor_->EndUndoAction();
  if (op == 'c') {
    EnterInsertForChange(from);
  } else {
    SetPos(from > 0 && from >= static_cast<size_t>(editor_->GetTextLength())
               ? static_cast<size_t>(editor_->GetTextLength())
               : from);
  }
}

void ScriptVim::IndentCurrentLines(int count, bool right) {
  const int line = editor_->GetCurrentLine();
  const int last = std::min(line + count - 1, editor_->GetLineCount() - 1);
  editor_->BeginUndoAction();
  for (int l = line; l <= last; ++l) {
    const int indent = editor_->GetLineIndentation(l);
    editor_->SetLineIndentation(l, std::max(0, indent + (right ? 4 : -4)));
  }
  editor_->EndUndoAction();
  SetPos(FirstNonBlank(static_cast<size_t>(editor_->PositionFromLine(line))));
}

void ScriptVim::ApplyOperator(char op, const Motion& motion) {
  if (!motion.valid) return;
  if (motion.linewise) {
    const int first_line = editor_->LineFromPosition(
        static_cast<int>(std::min(Pos(), motion.pos)));
    const int last_line = editor_->LineFromPosition(
        static_cast<int>(std::max(Pos(), motion.pos)));
    size_t from = static_cast<size_t>(editor_->PositionFromLine(first_line));
    size_t to = static_cast<size_t>(editor_->PositionFromLine(last_line + 1));
    if (last_line + 1 >= editor_->GetLineCount()) to = editor_->GetTextLength();
    if (op == 'c') {
      // `cc` leaves one empty line for the new text.
      to = std::min<size_t>(to, LineEnd(from));
      editor_->SetTargetStart(static_cast<int>(from));
      editor_->SetTargetEnd(static_cast<int>(to));
      const std::string text = Text();
      if (to > from) YankRange(from, to, false);
      editor_->BeginUndoAction();
      editor_->SetTargetStart(static_cast<int>(from));
      editor_->SetTargetEnd(static_cast<int>(to));
      editor_->ReplaceTarget("");
      editor_->EndUndoAction();
      EnterInsertForChange(from);
      return;
    }
    ApplyOperatorRange(op, from, to, true);
    return;
  }
  size_t from = std::min(Pos(), motion.pos);
  size_t to = std::max(Pos(), motion.pos);
  if (motion.inclusive && to < static_cast<size_t>(editor_->GetTextLength()))
    ++to;
  if (from == to && op != 'c') return;
  ApplyOperatorRange(op, from, to, false);
}

void ScriptVim::ApplyTextObject(char object, bool inner) {
  const std::string text = Text();
  const size_t pos = Clamp(Pos());
  size_t from = pos;
  size_t to = pos;
  switch (object) {
    case 'w': {
      if (text.empty()) return;
      size_t at = std::min(pos, text.size() - 1);
      if (!IsWordChar(text[at]) && at > 0) --at;
      from = at;
      while (from > 0 && IsWordChar(text[from - 1])) --from;
      to = at;
      while (to < text.size() && IsWordChar(text[to])) ++to;
      if (!inner) {
        while (to < text.size() && IsSpace(text[to])) ++to;
        if (from == to) return;
      }
      break;
    }
    case '"':
    case '\'':
    case '`': {
      const size_t begin = LineStart(pos);
      const size_t end = LineEnd(pos);
      size_t open = std::string::npos;
      for (size_t k = pos + 1; k-- > begin;) {
        if (text[k] == object) {
          open = k;
          break;
        }
      }
      if (open == std::string::npos) return;
      size_t close = std::string::npos;
      for (size_t k = open + 1; k < end; ++k) {
        if (text[k] == object) {
          close = k;
          break;
        }
      }
      if (close == std::string::npos) return;
      from = inner ? open + 1 : open;
      to = inner ? close : close + 1;
      break;
    }
    case '(':
    case ')':
    case 'b':
    case '[':
    case ']':
    case '{':
    case '}':
    case 'B':
    case '<':
    case '>': {
      char open_char = object;
      if (object == ')' || object == 'b') {
        open_char = '(';
      } else if (object == ']') {
        open_char = '[';
      } else if (object == '}' || object == 'B') {
        open_char = '{';
      } else if (object == '>') {
        open_char = '<';
      }
      size_t begin = LineStart(pos);
      size_t open = std::string::npos;
      for (size_t k = pos + 1; k-- > begin;) {
        if (text[k] == open_char) {
          open = k;
          break;
        }
      }
      if (open == std::string::npos) return;
      const int match = editor_->BraceMatch(static_cast<int>(open));
      if (match < 0) return;
      from = inner ? open + 1 : open;
      to = inner ? static_cast<size_t>(match) : static_cast<size_t>(match) + 1;
      break;
    }
    default:
      return;
  }
  if (to <= from) return;
  if (mode_ == Mode::kVisual || mode_ == Mode::kVisualLine) {
    SetPos(from);
    visual_anchor_ = to;
    mode_ = Mode::kVisual;
    UpdateVisualSelection();
    return;
  }
  ApplyOperatorRange(pending_operator_, from, to, false);
  pending_operator_ = 0;
}

void ScriptVim::Put(bool after, int count) {
  if (yank_.empty()) return;
  editor_->BeginUndoAction();
  for (int i = 0; i < count; ++i) {
    if (yank_linewise_) {
      const int line = editor_->LineFromPosition(static_cast<int>(Pos()));
      size_t at = 0;
      if (after) {
        at = line + 1 < editor_->GetLineCount()
                 ? static_cast<size_t>(editor_->PositionFromLine(line + 1))
                 : static_cast<size_t>(editor_->GetTextLength());
      } else {
        at = static_cast<size_t>(editor_->PositionFromLine(line));
      }
      editor_->InsertText(static_cast<int>(at), wxString::FromUTF8(yank_));
      SetPos(at);
    } else {
      size_t at = Pos();
      if (after && at < static_cast<size_t>(editor_->GetTextLength())) ++at;
      editor_->InsertText(static_cast<int>(at), wxString::FromUTF8(yank_));
      SetPos(at + yank_.size() - 1);
    }
  }
  editor_->EndUndoAction();
}

void ScriptVim::DeleteChar(int count, bool backward) {
  const size_t pos = Pos();
  size_t from = backward ? (pos >= static_cast<size_t>(count)
                                ? pos - static_cast<size_t>(count)
                                : 0)
                         : pos;
  size_t to = backward
                  ? pos
                  : std::min(pos + static_cast<size_t>(count),
                             static_cast<size_t>(editor_->GetTextLength()));
  if (to <= from) return;
  YankRange(from, to, false);
  editor_->BeginUndoAction();
  editor_->SetTargetStart(static_cast<int>(from));
  editor_->SetTargetEnd(static_cast<int>(to));
  editor_->ReplaceTarget("");
  editor_->EndUndoAction();
  SetPos(from);
}

void ScriptVim::ReplaceChar(int count, int character) {
  if (character <= 0 || character > 255) return;
  const std::string text = Text();
  size_t pos = Pos();
  editor_->BeginUndoAction();
  for (int i = 0; i < count && pos < text.size(); ++i) {
    if (text[pos] == '\n' || text[pos] == '\r') break;
    editor_->SetTargetStart(static_cast<int>(pos));
    editor_->SetTargetEnd(static_cast<int>(pos + 1));
    editor_->ReplaceTarget(
        wxString::FromUTF8(std::string(1, static_cast<char>(character))));
    ++pos;
  }
  editor_->EndUndoAction();
  SetPos(pos > 0 ? pos - 1 : pos);
}

void ScriptVim::ToggleCase(int count) {
  const std::string text = Text();
  size_t pos = Pos();
  std::string slice;
  const size_t end = std::min(pos + static_cast<size_t>(count), text.size());
  for (size_t i = pos; i < end; ++i) {
    char c = text[i];
    if (std::islower(static_cast<unsigned char>(c)))
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    else if (std::isupper(static_cast<unsigned char>(c)))
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    slice.push_back(c);
  }
  if (slice.empty()) return;
  editor_->BeginUndoAction();
  editor_->SetTargetStart(static_cast<int>(pos));
  editor_->SetTargetEnd(static_cast<int>(end));
  editor_->ReplaceTarget(wxString::FromUTF8(slice));
  editor_->EndUndoAction();
  SetPos(end);
}

void ScriptVim::JoinLines(int count) {
  editor_->BeginUndoAction();
  for (int i = 0; i < count; ++i) {
    const int line = editor_->LineFromPosition(static_cast<int>(Pos()));
    if (line + 1 >= editor_->GetLineCount()) break;
    const size_t end =
        LineEnd(static_cast<size_t>(editor_->PositionFromLine(line)));
    size_t next = end + 1;
    const std::string text = Text();
    while (next < text.size() && (text[next] == ' ' || text[next] == '\t'))
      ++next;
    editor_->SetTargetStart(static_cast<int>(end));
    editor_->SetTargetEnd(static_cast<int>(next));
    editor_->ReplaceTarget(" ");
  }
  editor_->EndUndoAction();
}

void ScriptVim::ShiftLines(int count, bool right) {
  IndentCurrentLines(count, right);
}

void ScriptVim::OpenLine(bool below) {
  const std::string text = Text();
  const int line = editor_->LineFromPosition(static_cast<int>(Pos()));
  const int indent = editor_->GetLineIndentation(line);
  const std::string pad(static_cast<size_t>(std::max(0, indent)), ' ');
  editor_->BeginUndoAction();
  if (below) {
    const size_t at = LineEnd(Pos());
    editor_->InsertText(static_cast<int>(at), wxString::FromUTF8("\n" + pad));
    editor_->EndUndoAction();
    EnterInsert(at + 1 + pad.size());
  } else {
    const size_t at = LineStart(Pos());
    editor_->InsertText(static_cast<int>(at), wxString::FromUTF8("\n" + pad));
    editor_->EndUndoAction();
    EnterInsert(at + 1 + pad.size());
  }
}

void ScriptVim::ChangeToLineEnd() {
  const size_t from = Pos();
  const size_t to = LineEnd(Pos());
  if (to > from) {
    YankRange(from, to, false);
    editor_->BeginUndoAction();
    editor_->SetTargetStart(static_cast<int>(from));
    editor_->SetTargetEnd(static_cast<int>(to));
    editor_->ReplaceTarget("");
    editor_->EndUndoAction();
  }
  EnterInsertForChange(from);
}

void ScriptVim::DeleteToLineEnd() {
  const size_t from = Pos();
  const size_t to = LineEnd(Pos());
  if (to > from) {
    YankRange(from, to, false);
    editor_->BeginUndoAction();
    editor_->SetTargetStart(static_cast<int>(from));
    editor_->SetTargetEnd(static_cast<int>(to));
    editor_->ReplaceTarget("");
    editor_->EndUndoAction();
  }
  SetPos(from);
}

void ScriptVim::YankLine(int count) {
  const int line = editor_->LineFromPosition(static_cast<int>(Pos()));
  const int last = std::min(line + count - 1, editor_->GetLineCount() - 1);
  const size_t from = static_cast<size_t>(editor_->PositionFromLine(line));
  size_t to = last + 1 < editor_->GetLineCount()
                  ? static_cast<size_t>(editor_->PositionFromLine(last + 1))
                  : static_cast<size_t>(editor_->GetTextLength());
  YankRange(from, to, true);
}

void ScriptVim::ScrollLines(int lines) { editor_->LineScroll(0, lines); }

void ScriptVim::Search(const std::string& pattern, bool backward, int count) {
  if (pattern.empty()) return;
  std::string needle = pattern;
  bool case_insensitive = false;
  if (needle.starts_with("\\c")) {
    case_insensitive = true;
    needle.erase(0, 2);
  } else if (needle.starts_with("\\C")) {
    needle.erase(0, 2);
  }
  std::string haystack = Text();
  if (case_insensitive) {
    std::transform(
        haystack.begin(), haystack.end(), haystack.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(
        needle.begin(), needle.end(), needle.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  }
  if (needle.empty()) return;
  size_t at = Pos();
  size_t found = std::string::npos;
  for (int i = 0; i < count; ++i) {
    if (backward) {
      found = haystack.rfind(needle, at > 0 ? at - 1 : 0);
      if (found == std::string::npos) found = haystack.rfind(needle);
      if (found == std::string::npos) break;
      at = found;
    } else {
      found = haystack.find(needle, at + (i == 0 ? 1 : 0));
      if (found == std::string::npos) found = haystack.find(needle);
      if (found == std::string::npos) break;
      at = found;
    }
  }
  if (found == std::string::npos) {
    Message("Pattern not found: " + wxString::FromUTF8(pattern));
    return;
  }
  SetPos(found);
}

void ScriptVim::SearchWord(bool backward, int count) {
  const std::string text = Text();
  size_t pos = WordUnderCursor();
  if (pos >= text.size()) return;
  size_t begin = pos;
  size_t end = pos;
  while (begin > 0 && IsWordChar(text[begin - 1])) --begin;
  while (end < text.size() && IsWordChar(text[end])) ++end;
  last_search_ = text.substr(begin, end - begin);
  last_search_backward_ = backward;
  Search(last_search_, backward, count);
}

void ScriptVim::ExecuteEx(const wxString& command) {
  std::string line = command.ToStdString();
  if (!line.empty() && line.front() == ':') line.erase(0, 1);
  while (!line.empty() &&
         std::isspace(static_cast<unsigned char>(line.front())))
    line.erase(0, 1);
  if (line.empty()) return;
  if (line == "w" || line == "write") {
    if (callbacks_.save && !callbacks_.save()) Message("Could not save");
    return;
  }
  if (line == "q" || line == "quit") {
    if (callbacks_.close) callbacks_.close(false);
    return;
  }
  if (line == "q!" || line == "quit!") {
    if (callbacks_.close) callbacks_.close(true);
    return;
  }
  if (line == "wq" || line == "x" || line == "xit") {
    if (!callbacks_.save || callbacks_.save()) {
      if (callbacks_.close) callbacks_.close(false);
    }
    return;
  }
  if (line == "wa" || line == "wall") {
    if (callbacks_.save) callbacks_.save();
    return;
  }
  if (line == "noh" || line == "nohlsearch") return;
  if (std::all_of(line.begin(), line.end(),
                  [](unsigned char c) { return std::isdigit(c); })) {
    MoveToLine(std::max(1, std::stoi(line)) - 1);
    return;
  }
  if (line.starts_with("s/") || line.starts_with("%s/") ||
      line.starts_with("s#") || line.starts_with("%s#")) {
    const bool all = line.front() == '%';
    if (all) line.erase(0, 1);
    if (line.size() < 4) return;
    const char delimiter = line[1];
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 2; i < line.size(); ++i) {
      if (line[i] == delimiter) {
        parts.push_back(current);
        current.clear();
      } else {
        current.push_back(line[i]);
      }
    }
    parts.push_back(current);
    if (parts.size() < 2 || parts[0].empty()) return;
    std::string text = Text();
    std::string result;
    if (all) {
      size_t start = 0;
      while (true) {
        const size_t found = text.find(parts[0], start);
        if (found == std::string::npos) {
          result.append(text, start, std::string::npos);
          break;
        }
        result.append(text, start, found - start);
        result += parts[1];
        start = found + parts[0].size();
        if (start >= text.size()) break;
      }
    } else {
      const size_t begin = LineStart(Pos());
      const size_t end = LineEnd(Pos());
      result = text;
      size_t at = begin;
      bool replaced = false;
      while (at < end) {
        const size_t found = result.find(parts[0], at);
        if (found == std::string::npos || found >= end) break;
        result.replace(found, parts[0].size(), parts[1]);
        at = found + parts[1].size();
        if (parts.size() < 3 || parts[2] != "g") break;
        replaced = true;
      }
      if (!replaced && result == text) return;
    }
    editor_->BeginUndoAction();
    editor_->SetText(wxString::FromUTF8(result));
    editor_->EndUndoAction();
    return;
  }
  Message("Not a command: " + command);
}

bool ScriptVim::HandleNormalKey(wxKeyEvent& evt) {
  const int code = evt.GetKeyCode();
  const int unicode = evt.GetUnicodeKey();
  const int key = unicode ? unicode : code;
  const bool ctrl = evt.ControlDown();

  if (ctrl) {
    switch (code) {
      case 'R':
      case 'r':
        editor_->Redo();
        return true;
      case 'D':
      case 'd':
        ScrollLines(std::max(1, editor_->LinesOnScreen() / 2));
        SetPos(Pos());
        return true;
      case 'U':
      case 'u':
        ScrollLines(-std::max(1, editor_->LinesOnScreen() / 2));
        SetPos(Pos());
        return true;
      case 'F':
      case 'f':
        ScrollLines(std::max(1, editor_->LinesOnScreen() - 2));
        SetPos(Pos());
        return true;
      case 'B':
      case 'b':
        ScrollLines(-std::max(1, editor_->LinesOnScreen() - 2));
        SetPos(Pos());
        return true;
      case 'E':
      case 'e':
        ScrollLines(1);
        return true;
      case 'Y':
      case 'y':
        ScrollLines(-1);
        return true;
      case 'S':
      case 's':
        if (callbacks_.save) callbacks_.save();
        return true;
      case 'W':
      case 'w':
        if (callbacks_.close) callbacks_.close(false);
        return true;
      default:
        return false;
    }
  }

  if (key == WXK_ESCAPE) {
    ClearPending();
    count_ = 0;
    has_count_ = false;
    return true;
  }

  if (pending_find_) {
    const char find = static_cast<char>(pending_find_);
    pending_find_ = 0;
    const Motion motion = FindMotion(find, TakeCount(), key);
    if (motion.valid) SetPos(motion.pos);
    last_find_command_ = find;
    last_find_char_ = key;
    return true;
  }
  if (pending_replace_) {
    const int count = TakeCount();
    pending_replace_ = 0;
    ReplaceChar(count, key);
    return true;
  }
  if (pending_object_) {
    const char which = pending_object_;
    pending_object_ = 0;
    ApplyTextObject(static_cast<char>(key), which == 'i');
    return true;
  }
  if (pending_g_) {
    const char which = pending_g_;
    pending_g_ = 0;
    Motion motion;
    if (which == 'g') {
      if (key == 'g') {
        const int count = TakeCount();
        motion.pos = static_cast<size_t>(editor_->PositionFromLine(
            count > 1 ? std::min(count - 1, editor_->GetLineCount() - 1) : 0));
        motion.linewise = true;
        motion.valid = true;
        if (pending_g_operator_) {
          ApplyOperator(pending_g_operator_, motion);
        } else {
          SetPos(motion.pos);
        }
        pending_g_operator_ = 0;
      } else if (key == '~' || key == 'u' || key == 'U') {
        pending_operator_ = static_cast<char>(key);
        pending_g_operator_ = 0;
      } else if (key == 'e') {
        const Motion m = MotionFor('e', TakeCount(), false);
        if (m.valid) SetPos(m.pos);
        pending_g_operator_ = 0;
      } else if (key == 'E') {
        const Motion m = MotionFor('E', TakeCount(), false);
        if (m.valid) SetPos(m.pos);
        pending_g_operator_ = 0;
      } else {
        pending_g_operator_ = 0;
      }
      return true;
    }
    if (which == 'z') {
      if (key == 'z') {
        editor_->ScrollToLine(editor_->GetCurrentLine() -
                              editor_->LinesOnScreen() / 2);
      } else if (key == 't') {
        editor_->ScrollToLine(editor_->GetCurrentLine());
      } else if (key == 'b') {
        editor_->ScrollToLine(editor_->GetCurrentLine() -
                              editor_->LinesOnScreen() + 1);
      }
      return true;
    }
    if (which == 'Z') {
      if (key == 'Z') {
        if (!callbacks_.save || callbacks_.save()) {
          if (callbacks_.close) callbacks_.close(false);
        }
      } else if (key == 'Q') {
        if (callbacks_.close) callbacks_.close(true);
      }
      return true;
    }
    return true;
  }
  if (pending_operator_) {
    if (key == pending_operator_) {
      const int count = TakeCount();
      const int line = editor_->LineFromPosition(static_cast<int>(Pos()));
      const int last = std::min(line + count - 1, editor_->GetLineCount() - 1);
      const size_t from = static_cast<size_t>(editor_->PositionFromLine(line));
      size_t to = last + 1 < editor_->GetLineCount()
                      ? static_cast<size_t>(editor_->PositionFromLine(last + 1))
                      : static_cast<size_t>(editor_->GetTextLength());
      if (pending_operator_ == 'c') {
        to = std::min<size_t>(to, LineEnd(from));
        ApplyOperatorRange('c', from, to, false);
      } else {
        ApplyOperatorRange(pending_operator_, from, to, true);
      }
      pending_operator_ = 0;
      return true;
    }
    if (key == 'i' || key == 'a') {
      pending_object_ = static_cast<char>(key);
      return true;
    }
    if (std::isdigit(static_cast<unsigned char>(key)) &&
        (has_count_ || key != '0')) {
      count_ = count_ * 10 + (key - '0');
      has_count_ = true;
      return true;
    }
    if (key == 'g') {
      // g~ / gu / gU and dgg are handled when the next key arrives.
      pending_g_ = 'g';
      pending_g_operator_ = pending_operator_;
      pending_operator_ = 0;
      return true;
    }
    if (key == 'f' || key == 'F' || key == 't' || key == 'T') {
      pending_find_ = static_cast<char>(key);
      return true;
    }
    const int count = TakeCount();
    const char op = pending_operator_;
    const Motion motion = MotionFor(key, count, true);
    if (motion.valid) {
      ApplyOperator(op, motion);
      last_change_ = [this, op, key, count]() {
        const Motion replay = MotionFor(key, count, true);
        if (replay.valid) ApplyOperator(op, replay);
      };
    }
    pending_operator_ = 0;
    return true;
  }

  if (std::isdigit(static_cast<unsigned char>(key))) {
    if (key == '0' && !has_count_) {
      const Motion m = MotionFor('0', 1, false);
      if (m.valid) SetPos(m.pos);
      return true;
    }
    count_ = count_ * 10 + (key - '0');
    has_count_ = true;
    return true;
  }

  switch (key) {
    case 'h':
    case 'l':
    case 'j':
    case 'k':
    case 'w':
    case 'W':
    case 'b':
    case 'B':
    case 'e':
    case 'E':
    case '0':
    case '^':
    case '$':
    case '%':
    case '{':
    case '}':
    case 'H':
    case 'M':
    case 'L':
    case WXK_LEFT:
    case WXK_RIGHT:
    case WXK_UP:
    case WXK_DOWN:
    case WXK_HOME:
    case WXK_END:
    case WXK_RETURN: {
      const Motion motion = MotionFor(key, TakeCount(), false);
      if (motion.valid) SetPos(motion.pos);
      return true;
    }
    case 'G': {
      const int count = TakeCount();
      MoveToLine(count > 1 ? count - 1 : editor_->GetLineCount() - 1);
      return true;
    }
    case '+':
      MoveToLine(editor_->GetCurrentLine() + TakeCount());
      return true;
    case '-':
      MoveToLine(editor_->GetCurrentLine() - TakeCount());
      return true;
    case ' ':
      SetPos(Pos() + static_cast<size_t>(TakeCount()));
      return true;
    case WXK_BACK: {
      const size_t count = static_cast<size_t>(TakeCount());
      SetPos(Pos() > count ? Pos() - count : 0);
      return true;
    }
    case WXK_PAGEUP:
      ScrollLines(-(editor_->LinesOnScreen() - 1));
      return true;
    case WXK_PAGEDOWN:
      ScrollLines(editor_->LinesOnScreen() - 1);
      return true;
    case 'i':
      EnterInsert(Pos());
      return true;
    case 'a':
      EnterInsert(Pos() < static_cast<size_t>(editor_->GetTextLength())
                      ? Pos() + 1
                      : Pos());
      return true;
    case 'I':
      EnterInsert(FirstNonBlank(Pos()));
      return true;
    case 'A':
      EnterInsert(LineEnd(Pos()));
      return true;
    case 'R':
      SetPos(Pos());
      insert_start_ = Pos();
      SetMode(Mode::kReplace);
      return true;
    case 'o':
      OpenLine(true);
      return true;
    case 'O':
      OpenLine(false);
      return true;
    case 'v':
      visual_anchor_ = Pos();
      SetMode(Mode::kVisual);
      UpdateVisualSelection();
      return true;
    case 'V':
      visual_anchor_ = Pos();
      SetMode(Mode::kVisualLine);
      UpdateVisualSelection();
      return true;
    case 'd':
    case 'c':
    case 'y':
    case '>':
    case '<':
      pending_operator_ = static_cast<char>(key);
      return true;
    case 'D':
      DeleteToLineEnd();
      return true;
    case 'C':
      ChangeToLineEnd();
      return true;
    case 'Y':
      YankLine(TakeCount());
      return true;
    case 'x': {
      const int count = TakeCount();
      DeleteChar(count, false);
      last_change_ = [this, count]() { DeleteChar(count, false); };
      return true;
    }
    case 'X': {
      const int count = TakeCount();
      DeleteChar(count, true);
      last_change_ = [this, count]() { DeleteChar(count, true); };
      return true;
    }
    case 's':
      DeleteChar(TakeCount(), false);
      EnterInsert(Pos());
      return true;
    case 'S': {
      const int line = editor_->GetCurrentLine();
      const size_t from = static_cast<size_t>(editor_->PositionFromLine(line));
      const size_t to = LineEnd(from);
      ApplyOperatorRange('c', from, to, false);
      return true;
    }
    case 'r':
      pending_replace_ = 'r';
      return true;
    case '~': {
      const int count = TakeCount();
      ToggleCase(count);
      last_change_ = [this, count]() { ToggleCase(count); };
      return true;
    }
    case 'J': {
      const int count = TakeCount();
      JoinLines(count);
      last_change_ = [this, count]() { JoinLines(count); };
      return true;
    }
    case 'p': {
      const int count = TakeCount();
      Put(true, count);
      last_change_ = [this, count]() { Put(true, count); };
      return true;
    }
    case 'P': {
      const int count = TakeCount();
      Put(false, count);
      last_change_ = [this, count]() { Put(false, count); };
      return true;
    }
    case 'u':
      editor_->Undo();
      return true;
    case '.':
      if (last_change_) last_change_();
      return true;
    case 'f':
    case 'F':
    case 't':
    case 'T':
      pending_find_ = static_cast<char>(key);
      return true;
    case ';': {
      if (last_find_command_ == 0) return true;
      const Motion m =
          FindMotion(last_find_command_, TakeCount(), last_find_char_);
      if (m.valid) SetPos(m.pos);
      return true;
    }
    case ',': {
      if (last_find_command_ == 0) return true;
      char reverse = last_find_command_;
      if (reverse == 'f')
        reverse = 'F';
      else if (reverse == 'F')
        reverse = 'f';
      else if (reverse == 't')
        reverse = 'T';
      else if (reverse == 'T')
        reverse = 't';
      const Motion m = FindMotion(reverse, TakeCount(), last_find_char_);
      if (m.valid) SetPos(m.pos);
      return true;
    }
    case 'g':
      pending_g_ = 'g';
      return true;
    case 'z':
      pending_g_ = 'z';
      return true;
    case 'Z':
      pending_g_ = 'Z';
      return true;
    case ':':
      if (callbacks_.prompt) {
        callbacks_.prompt(":",
                          [this](const wxString& text) { ExecuteEx(text); });
      }
      return true;
    case '/':
      if (callbacks_.prompt) {
        callbacks_.prompt("/", [this](const wxString& text) {
          last_search_ = text.Mid(1).ToStdString();
          last_search_backward_ = false;
          Search(last_search_, false, 1);
        });
      }
      return true;
    case '?':
      if (callbacks_.prompt) {
        callbacks_.prompt("?", [this](const wxString& text) {
          last_search_ = text.Mid(1).ToStdString();
          last_search_backward_ = true;
          Search(last_search_, true, 1);
        });
      }
      return true;
    case 'n':
      if (!last_search_.empty())
        Search(last_search_, last_search_backward_, TakeCount());
      return true;
    case 'N':
      if (!last_search_.empty())
        Search(last_search_, !last_search_backward_, TakeCount());
      return true;
    case '*':
      SearchWord(false, TakeCount());
      return true;
    case '#':
      SearchWord(true, TakeCount());
      return true;
    default:
      // Consume unknown printable keys so normal mode never inserts text.
      return unicode >= 32;
  }
}

bool ScriptVim::HandleVisualKey(wxKeyEvent& evt) {
  const int code = evt.GetKeyCode();
  const int unicode = evt.GetUnicodeKey();
  const int key = unicode ? unicode : code;
  if (evt.ControlDown()) return false;
  if (key == WXK_ESCAPE || key == 'v' || key == 'V') {
    const size_t pos = Pos();
    mode_ = Mode::kNormal;
    SetPos(pos);
    SetMode(Mode::kNormal);
    visual_anchor_ = pos;
    return true;
  }
  if (key == 'o') {
    const size_t pos = Pos();
    SetPos(visual_anchor_);
    visual_anchor_ = pos;
    UpdateVisualSelection();
    return true;
  }
  if (key == 'd' || key == 'y' || key == 'c' || key == 'x' || key == 's') {
    const int from = static_cast<int>(visual_anchor_);
    const int to = editor_->GetCurrentPos();
    const int begin = std::min(from, to);
    int end = std::max(from, to);
    if (mode_ == Mode::kVisualLine) {
      const int first = editor_->LineFromPosition(begin);
      const int last = editor_->LineFromPosition(end);
      size_t line_from = static_cast<size_t>(editor_->PositionFromLine(first));
      size_t line_to =
          last + 1 < editor_->GetLineCount()
              ? static_cast<size_t>(editor_->PositionFromLine(last + 1))
              : static_cast<size_t>(editor_->GetTextLength());
      if (key == 'y')
        ApplyOperatorRange('y', line_from, line_to, true);
      else if (key == 'c')
        ApplyOperatorRange('c', line_from, line_to, false);
      else
        ApplyOperatorRange('d', line_from, line_to, true);
      if (key == 'c') {
        SetMode(Mode::kInsert);
        return true;
      }
    } else {
      if (end < editor_->GetTextLength()) ++end;
      const char op = key == 'x' || key == 's' ? 'd' : static_cast<char>(key);
      ApplyOperatorRange(op, static_cast<size_t>(begin),
                         static_cast<size_t>(end), false);
      if (key == 'c' || key == 's') {
        EnterInsertForChange(static_cast<size_t>(begin));
        return true;
      }
    }
    const size_t pos = begin;
    SetPos(pos);
    SetMode(Mode::kNormal);
    visual_anchor_ = pos;
    return true;
  }
  if (key == '~' || key == 'u' || key == 'U' || key == '>' || key == '<') {
    const int from = static_cast<int>(visual_anchor_);
    const int to = editor_->GetCurrentPos();
    const int begin = std::min(from, to);
    int end = std::max(from, to);
    if (end < editor_->GetTextLength()) ++end;
    if (key == '>' || key == '<') {
      const int first = editor_->LineFromPosition(begin);
      const int last = editor_->LineFromPosition(end);
      const int old_line = editor_->GetCurrentLine();
      editor_->SetCurrentPos(editor_->PositionFromLine(first));
      IndentCurrentLines(std::max(1, last - first + 1), key == '>');
      (void)old_line;
    } else {
      ApplyOperatorRange(static_cast<char>(key), static_cast<size_t>(begin),
                         static_cast<size_t>(end), false);
    }
    const size_t pos = begin;
    SetPos(pos);
    SetMode(Mode::kNormal);
    return true;
  }
  if (key == 'p' || key == 'P') {
    const int from = static_cast<int>(visual_anchor_);
    const int to = editor_->GetCurrentPos();
    const int begin = std::min(from, to);
    int end = std::max(from, to);
    if (end < editor_->GetTextLength()) ++end;
    if (end > begin) {
      editor_->BeginUndoAction();
      editor_->SetTargetStart(begin);
      editor_->SetTargetEnd(end);
      editor_->ReplaceTarget(wxString::FromUTF8(yank_));
      editor_->EndUndoAction();
    }
    SetPos(static_cast<size_t>(begin));
    SetMode(Mode::kNormal);
    return true;
  }
  if (pending_find_) {
    const char find = static_cast<char>(pending_find_);
    pending_find_ = 0;
    const Motion m = FindMotion(find, TakeCount(), key);
    if (m.valid) {
      SetPos(m.pos);
      UpdateVisualSelection();
    }
    last_find_command_ = find;
    last_find_char_ = key;
    return true;
  }
  if (pending_g_) {
    pending_g_ = 0;
    if (key == 'g') {
      const int count = TakeCount();
      MoveToLine(count > 1 ? count - 1 : 0);
      UpdateVisualSelection();
    }
    return true;
  }
  if (pending_object_ == 'i' || pending_object_ == 'a') {
    const char which = pending_object_;
    pending_object_ = 0;
    ApplyTextObject(static_cast<char>(key), which == 'i');
    return true;
  }
  if (key == 'i' || key == 'a') {
    pending_object_ = static_cast<char>(key);
    return true;
  }
  if (key == 'g') {
    pending_g_ = 'g';
    return true;
  }
  if (key == 'f' || key == 'F' || key == 't' || key == 'T') {
    pending_find_ = static_cast<char>(key);
    return true;
  }
  if (std::isdigit(static_cast<unsigned char>(key))) {
    if (key == '0' && !has_count_) {
      SetPos(LineStart(Pos()));
      UpdateVisualSelection();
      return true;
    }
    count_ = count_ * 10 + (key - '0');
    has_count_ = true;
    return true;
  }
  if (key == 'G') {
    const int count = TakeCount();
    MoveToLine(count > 1 ? count - 1 : editor_->GetLineCount() - 1);
    UpdateVisualSelection();
    return true;
  }
  const Motion motion = MotionFor(key, TakeCount(), false);
  if (motion.valid) {
    SetPos(motion.pos);
    UpdateVisualSelection();
  }
  return true;
}

}  // namespace Mercury::EditorUI
