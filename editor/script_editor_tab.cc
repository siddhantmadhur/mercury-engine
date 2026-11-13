/*
 * script_editor_tab.cc
 */
#include "script_editor_tab.h"

#include <wx/filename.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "script_editor_panel.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace {

constexpr int kErrorMarker = 0;
constexpr int kWarningMarker = 1;
constexpr int kErrorIndicator = 8;
constexpr int kWarningIndicator = 9;

wxColour Background() { return wxColour(30, 32, 36); }
wxColour Foreground() { return wxColour(219, 223, 229); }
wxColour Muted() { return wxColour(116, 123, 134); }
wxColour Keyword() { return wxColour(197, 134, 192); }
wxColour TypeColour() { return wxColour(78, 201, 176); }
wxColour StringColour() { return wxColour(206, 145, 120); }
wxColour CommentColour() { return wxColour(106, 153, 85); }
wxColour NumberColour() { return wxColour(181, 206, 168); }
wxColour OperatorColour() { return wxColour(212, 212, 212); }
wxColour CurrentLineColour() { return wxColour(38, 41, 47); }
wxColour Selection() { return wxColour(38, 79, 120); }

const char* kAngelScriptKeywords =
    "abstract auto bool break case cast class const continue default do double "
    "else enum explicit external false final float for from funcdef if import "
    "in inout int int8 int16 int32 int64 interface is mixin namespace null out "
    "override private protected property return shared string super switch "
    "this "
    "true typedef uint uint8 uint16 uint32 uint64 using void while";

wxString ReadFileUtf8(const std::filesystem::path& path, std::string* out,
                      std::string* error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error) *error = "Could not open " + path.string();
    return {};
  }
  std::ostringstream text;
  text << in.rdbuf();
  *out = text.str();
  return wxString::FromUTF8(*out);
}

}  // namespace

ScriptEditorTab::ScriptEditorTab(wxWindow* parent, ScriptEditorPanel* panel,
                                 std::filesystem::path file,
                                 std::string res_path)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxBORDER_NONE),
      panel_(panel),
      file_(std::move(file)),
      res_path_(std::move(res_path)) {
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  editor_ = new wxStyledTextCtrl(this, wxID_ANY);
  sizer->Add(editor_, 1, wxEXPAND);
  SetSizer(sizer);

  editor_->SetCodePage(wxSTC_CP_UTF8);
  ApplyTheme();

  ScriptVim::Callbacks callbacks;
  callbacks.save = [this]() { return panel_ && panel_->SaveActive(); };
  callbacks.close = [this](bool force) {
    if (panel_) panel_->TabCloseRequested(this, force);
    return true;
  };
  callbacks.prompt = [this](const wxString& prefix,
                            std::function<void(const wxString&)> on_submit) {
    if (panel_) panel_->ShowCommandLine(prefix, std::move(on_submit));
  };
  callbacks.status_changed = [this]() {
    if (panel_) panel_->TabModeChanged(this);
  };
  callbacks.message = [this](const wxString& message) {
    if (panel_) panel_->SetStatusMessage(message);
  };
  vim_ = std::make_unique<ScriptVim>(editor_, std::move(callbacks));

  BindEvents();
}

bool ScriptEditorTab::Load(std::string* error) {
  std::string content;
  const wxString text = ReadFileUtf8(file_, &content, error);
  if (text.empty() && !content.empty()) {
    if (error) *error = "Could not decode " + file_.string();
    return false;
  }
  eol_ = content.find("\r\n") != std::string::npos
             ? "crlf"
             : (content.find('\r') != std::string::npos ? "cr" : "lf");
  editor_->SetEOLMode(eol_ == "crlf"
                          ? wxSTC_EOL_CRLF
                          : (eol_ == "cr" ? wxSTC_EOL_CR : wxSTC_EOL_LF));
  editor_->SetTextRaw(content.c_str());
  editor_->EmptyUndoBuffer();
  editor_->SetSavePoint();
  UpdateLineNumberMargin();
  return true;
}

bool ScriptEditorTab::ReloadFromDisk(std::string* error) { return Load(error); }

bool ScriptEditorTab::Save(std::string* error) {
  const wxCharBuffer buffer = editor_->GetTextRaw();
  std::ofstream out(file_, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error) *error = "Could not write " + file_.string();
    return false;
  }
  out.write(buffer.data(), static_cast<std::streamsize>(buffer.length()));
  if (!out) {
    if (error) *error = "Could not write " + file_.string();
    return false;
  }
  editor_->SetSavePoint();
  return true;
}

bool ScriptEditorTab::IsDirty() const { return editor_->GetModify(); }

void ScriptEditorTab::FocusEditor() {
  editor_->SetFocus();
  editor_->EnsureCaretVisible();
}

void ScriptEditorTab::GoTo(int line, int column) {
  if (line <= 0) line = 1;
  line = std::min(line, editor_->GetLineCount());
  const int target = editor_->PositionFromLine(line - 1);
  const int pos = std::max(target, target + std::max(0, column - 1));
  editor_->SetSelection(pos, pos);
  editor_->EnsureCaretVisible();
  editor_->SetFocus();
}

int ScriptEditorTab::CurrentLine() const {
  return editor_->GetCurrentLine() + 1;
}

int ScriptEditorTab::CurrentColumn() const {
  return editor_->GetColumn(editor_->GetCurrentPos()) + 1;
}

void ScriptEditorTab::SetVimEnabled(bool enabled) {
  vim_enabled_ = enabled;
  if (vim_) vim_->SetEnabled(enabled);
  if (enabled) {
    if (panel_) panel_->SetStatusMessage("Vim mode enabled");
  }
}

bool ScriptEditorTab::VimEnabled() const { return vim_enabled_; }

const char* ScriptEditorTab::ModeName() const {
  if (!vim_enabled_) return "EDIT";
  return vim_ ? vim_->ModeName() : "NORMAL";
}

void ScriptEditorTab::ApplyTheme() {
  wxFont font = MonospaceFont();
  for (int style = 0; style < 128; ++style) {
    editor_->StyleSetFont(style, font);
  }
  editor_->StyleSetFont(wxSTC_STYLE_LINENUMBER, font);
  editor_->StyleSetForeground(wxSTC_STYLE_DEFAULT, Foreground());
  editor_->StyleSetBackground(wxSTC_STYLE_DEFAULT, Background());
  editor_->SetLexer(wxSTC_LEX_CPP);
  editor_->StyleClearAll();
  ApplyThemeStyles();

  editor_->SetMargins(8, 8);
  editor_->SetTabWidth(4);
  editor_->SetIndent(4);
  editor_->SetUseTabs(false);
  editor_->SetBackSpaceUnIndents(true);
  editor_->SetIndentationGuides(wxSTC_IV_LOOKBOTH);
  editor_->SetWrapMode(wxSTC_WRAP_NONE);
  editor_->SetScrollWidthTracking(true);
  editor_->SetCaretLineVisible(true);
  editor_->SetCaretLineBackground(CurrentLineColour());
  editor_->SetSelForeground(true, Foreground());
  editor_->SetSelBackground(true, Selection());
  editor_->SetCaretForeground(Foreground());
  editor_->SetCaretWidth(2);
  editor_->AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

  editor_->SetMarginType(0, wxSTC_MARGIN_NUMBER);
  editor_->SetMarginWidth(0, 42);
  editor_->SetMarginType(1, wxSTC_MARGIN_SYMBOL);
  editor_->SetMarginMask(1, (1 << kErrorMarker) | (1 << kWarningMarker));
  editor_->SetMarginWidth(1, 14);
  editor_->SetMarginSensitive(1, false);

  editor_->SetMarginType(2, wxSTC_MARGIN_SYMBOL);
  editor_->SetMarginMask(
      2, (1 << wxSTC_MARKNUM_FOLDER) | (1 << wxSTC_MARKNUM_FOLDEROPEN) |
             (1 << wxSTC_MARKNUM_FOLDERSUB) | (1 << wxSTC_MARKNUM_FOLDERTAIL) |
             (1 << wxSTC_MARKNUM_FOLDEREND) |
             (1 << wxSTC_MARKNUM_FOLDEROPENMID) |
             (1 << wxSTC_MARKNUM_FOLDERMIDTAIL));
  editor_->SetMarginWidth(2, 14);
  editor_->SetMarginSensitive(2, true);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDER, wxSTC_MARK_BOXPLUS);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARK_BOXMINUS);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDERSUB, wxSTC_MARK_VLINE);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARK_LCORNER);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDEREND, wxSTC_MARK_BOXPLUSCONNECTED);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDEROPENMID,
                        wxSTC_MARK_BOXMINUSCONNECTED);
  editor_->MarkerDefine(wxSTC_MARKNUM_FOLDERMIDTAIL, wxSTC_MARK_TCORNER);
  editor_->SetFoldFlags(wxSTC_FOLDFLAG_LINEAFTER_CONTRACTED);
  editor_->SetAutomaticFold(wxSTC_AUTOMATICFOLD_SHOW |
                            wxSTC_AUTOMATICFOLD_CHANGE);

  editor_->MarkerDefine(kErrorMarker, wxSTC_MARK_CIRCLE);
  editor_->MarkerSetBackground(kErrorMarker, wxColour(214, 84, 84));
  editor_->MarkerDefine(kWarningMarker, wxSTC_MARK_CIRCLE);
  editor_->MarkerSetBackground(kWarningMarker, wxColour(214, 170, 72));
  editor_->IndicatorSetStyle(kErrorIndicator, wxSTC_INDIC_SQUIGGLE);
  editor_->IndicatorSetForeground(kErrorIndicator, wxColour(224, 92, 92));
  editor_->IndicatorSetStyle(kWarningIndicator, wxSTC_INDIC_SQUIGGLELOW);
  editor_->IndicatorSetForeground(kWarningIndicator, wxColour(214, 170, 72));
  editor_->AnnotationSetVisible(wxSTC_ANNOTATION_BOXED);

  editor_->SetProperty("lexer.cpp.track.preprocessor", "0");
  editor_->SetProperty("lexer.cpp.backquoted.strings", "0");
  editor_->SetProperty("fold", "1");
  editor_->SetProperty("fold.compact", "0");
  editor_->SetProperty("fold.comment", "1");
  editor_->SetProperty("fold.preprocessor", "0");
  editor_->SetKeyWords(0, wxString::FromUTF8(kAngelScriptKeywords));
  ApplyTypeKeywords();
  UpdateLineNumberMargin();
}

void ScriptEditorTab::ApplyThemeStyles() {
  auto style = [&](int id, const wxColour& colour, bool bold = false) {
    editor_->StyleSetForeground(id, colour);
    editor_->StyleSetBackground(id, Background());
    if (bold) {
      wxFont font = editor_->StyleGetFont(id);
      font.SetWeight(wxFONTWEIGHT_BOLD);
      editor_->StyleSetFont(id, font);
    }
  };
  editor_->StyleSetForeground(wxSTC_STYLE_DEFAULT, Foreground());
  editor_->StyleSetBackground(wxSTC_STYLE_DEFAULT, Background());
  editor_->StyleSetForeground(wxSTC_STYLE_LINENUMBER, Muted());
  editor_->StyleSetBackground(wxSTC_STYLE_LINENUMBER, Background());
  style(wxSTC_C_DEFAULT, Foreground());
  style(wxSTC_C_COMMENT, CommentColour());
  style(wxSTC_C_COMMENTLINE, CommentColour());
  style(wxSTC_C_COMMENTDOC, CommentColour());
  style(wxSTC_C_COMMENTLINEDOC, CommentColour());
  style(wxSTC_C_COMMENTDOCKEYWORD, CommentColour());
  style(wxSTC_C_COMMENTDOCKEYWORDERROR, CommentColour());
  style(wxSTC_C_NUMBER, NumberColour());
  style(wxSTC_C_WORD, Keyword());
  style(wxSTC_C_WORD2, TypeColour());
  style(wxSTC_C_STRING, StringColour());
  style(wxSTC_C_CHARACTER, StringColour());
  style(wxSTC_C_STRINGEOL, StringColour());
  style(wxSTC_C_STRINGRAW, StringColour());
  style(wxSTC_C_OPERATOR, OperatorColour());
  style(wxSTC_C_IDENTIFIER, Foreground());
  style(wxSTC_C_PREPROCESSOR, Keyword());
  style(wxSTC_C_GLOBALCLASS, TypeColour());
  editor_->StyleSetForeground(wxSTC_STYLE_BRACELIGHT, wxColour(255, 215, 100));
  editor_->StyleSetBackground(wxSTC_STYLE_BRACELIGHT, wxColour(60, 60, 40));
  editor_->StyleSetForeground(wxSTC_STYLE_BRACEBAD, wxColour(255, 110, 110));
  editor_->StyleSetBackground(wxSTC_STYLE_BRACEBAD, wxColour(60, 36, 36));
}

void ScriptEditorTab::ApplyTypeKeywords() {
  if (!panel_) return;
  const std::string& keywords = panel_->NativeSymbols().empty()
                                    ? std::string()
                                    : panel_->LanguageTypeKeywords();
  if (!keywords.empty()) {
    editor_->SetKeyWords(1, wxString::FromUTF8(keywords));
  }
}

void ScriptEditorTab::UpdateLineNumberMargin() {
  if (!editor_) return;
  const int lines = std::max(1, editor_->GetLineCount());
  wxString sample = wxString::Format("%d", lines);
  sample += "00";
  const int width = editor_->TextWidth(wxSTC_STYLE_LINENUMBER, sample) + 12;
  if (editor_->GetMarginWidth(0) != width) {
    editor_->SetMarginWidth(0, width);
  }
}

void ScriptEditorTab::ClearDiagnostics() {
  editor_->MarkerDeleteAll(kErrorMarker);
  editor_->MarkerDeleteAll(kWarningMarker);
  editor_->SetIndicatorCurrent(kErrorIndicator);
  editor_->IndicatorClearRange(0, editor_->GetTextLength());
  editor_->SetIndicatorCurrent(kWarningIndicator);
  editor_->IndicatorClearRange(0, editor_->GetTextLength());
  editor_->AnnotationClearAll();
  diagnostics_.clear();
}

void ScriptEditorTab::ApplyDiagnostics(
    const std::vector<ScriptDiagnostic>& diagnostics) {
  ClearDiagnostics();
  for (const ScriptDiagnostic& diagnostic : diagnostics) {
    if (diagnostic.file != res_path_) continue;
    const int line =
        std::max(0, std::min(diagnostic.line - 1, editor_->GetLineCount() - 1));
    if (line < 0) continue;
    const int marker = diagnostic.warning ? kWarningMarker : kErrorMarker;
    editor_->MarkerAdd(line, marker);
    const int begin = editor_->PositionFromLine(line);
    const int end = editor_->GetLineEndPosition(line);
    if (end > begin) {
      editor_->SetIndicatorCurrent(diagnostic.warning ? kWarningIndicator
                                                      : kErrorIndicator);
      editor_->IndicatorFillRange(begin, end - begin);
    }
    editor_->AnnotationSetText(line, wxString::FromUTF8(diagnostic.message));
    editor_->AnnotationSetStyle(line, diagnostic.warning
                                          ? wxSTC_STYLE_LINENUMBER
                                          : wxSTC_STYLE_BRACEBAD);
    diagnostics_.push_back(diagnostic);
  }
}

void ScriptEditorTab::RefreshIndicators() {
  editor_->SetIndicatorCurrent(kErrorIndicator);
  editor_->IndicatorClearRange(0, editor_->GetTextLength());
  editor_->SetIndicatorCurrent(kWarningIndicator);
  editor_->IndicatorClearRange(0, editor_->GetTextLength());
  for (const ScriptDiagnostic& diagnostic : diagnostics_) {
    const int line =
        std::max(0, std::min(diagnostic.line - 1, editor_->GetLineCount() - 1));
    if (line < 0) continue;
    const int begin = editor_->PositionFromLine(line);
    const int end = editor_->GetLineEndPosition(line);
    if (end > begin) {
      editor_->SetIndicatorCurrent(diagnostic.warning ? kWarningIndicator
                                                      : kErrorIndicator);
      editor_->IndicatorFillRange(begin, end - begin);
    }
  }
}

bool ScriptEditorTab::IsInCode(int pos) const {
  if (pos < 0) return true;
  switch (editor_->GetStyleAt(pos)) {
    case wxSTC_C_COMMENT:
    case wxSTC_C_COMMENTLINE:
    case wxSTC_C_COMMENTDOC:
    case wxSTC_C_COMMENTLINEDOC:
    case wxSTC_C_COMMENTDOCKEYWORD:
    case wxSTC_C_COMMENTDOCKEYWORDERROR:
    case wxSTC_C_STRING:
    case wxSTC_C_CHARACTER:
    case wxSTC_C_STRINGEOL:
    case wxSTC_C_STRINGRAW:
      return false;
    default:
      return true;
  }
}

std::string ScriptEditorTab::CurrentWord(int* prefix_length) const {
  const int pos = editor_->GetCurrentPos();
  const int start = editor_->WordStartPosition(pos, true);
  if (prefix_length) *prefix_length = std::max(0, pos - start);
  return editor_->GetTextRange(start, pos).utf8_str().data();
}

void ScriptEditorTab::ShowCompletion(bool force) {
  if (!panel_) return;
  if (vim_enabled_ && vim_ && vim_->GetMode() != ScriptVim::Mode::kInsert &&
      vim_->GetMode() != ScriptVim::Mode::kReplace) {
    return;
  }
  const int pos = editor_->GetCurrentPos();
  if (!IsInCode(pos - 1)) return;
  int prefix_length = 0;
  const std::string current = CurrentWord(&prefix_length);
  (void)current;
  const char before =
      pos > 0 ? static_cast<char>(editor_->GetCharAt(pos - 1)) : 0;
  if (!force && before != '.' && before != ':' && prefix_length < 2) return;

  const std::string text = editor_->GetText().utf8_str().data();
  const auto items =
      CompleteAt(panel_->Workspace(), res_path_, text, static_cast<size_t>(pos),
                 panel_->NativeSymbols());
  if (items.empty()) return;
  wxString list;
  completion_details_.clear();
  for (const CompletionItem& item : items) {
    if (list.empty()) {
      list = wxString::FromUTF8(item.label);
    } else {
      list += " ";
      list += wxString::FromUTF8(item.label);
    }
    completion_details_[item.label] = item.detail;
  }
  editor_->AutoCompSetSeparator(' ');
  editor_->AutoCompSetIgnoreCase(true);
  editor_->AutoCompSetAutoHide(true);
  editor_->AutoCompSetDropRestOfWord(true);
  editor_->AutoCompSetMaxHeight(12);
  editor_->AutoCompSetMaxWidth(72);
  editor_->AutoCompSetCancelAtStart(true);
  editor_->AutoCompShow(prefix_length, list);
}

void ScriptEditorTab::ShowCallTip() {
  if (!panel_) return;
  const int pos = editor_->GetCurrentPos();
  if (pos <= 0 || editor_->GetCharAt(pos - 1) != '(') return;
  const int start = editor_->WordStartPosition(pos - 1, true);
  const wxString name = editor_->GetTextRange(start, pos - 1);
  if (name.empty()) return;
  const std::string wanted = name.utf8_str().data();
  std::string signature;
  for (const ParsedScript& file : panel_->Workspace().Files()) {
    for (const ScriptSymbol& symbol : file.symbols) {
      if (symbol.name == wanted && !symbol.signature.empty() &&
          (symbol.kind == ScriptSymbolKind::kFunction ||
           symbol.kind == ScriptSymbolKind::kMethod)) {
        signature = symbol.signature;
        break;
      }
    }
    if (!signature.empty()) break;
  }
  if (signature.empty()) {
    for (const ScriptSymbol& symbol : panel_->NativeSymbols()) {
      if (symbol.name == wanted && !symbol.signature.empty() &&
          (symbol.kind == ScriptSymbolKind::kFunction ||
           symbol.kind == ScriptSymbolKind::kMethod)) {
        signature = symbol.signature;
        break;
      }
    }
  }
  if (!signature.empty()) {
    editor_->CallTipShow(pos, wxString::FromUTF8(signature));
  }
}

void ScriptEditorTab::GoToDefinition(int pos) {
  if (!panel_) return;
  const std::string text = editor_->GetText().utf8_str().data();
  const auto definitions = FindDefinitionsAt(
      panel_->Workspace(), res_path_, text,
      static_cast<size_t>(std::max(0, pos)), panel_->NativeSymbols());
  if (definitions.empty()) {
    panel_->SetStatusMessage("No definition found");
    return;
  }
  panel_->OpenLocation(definitions.front().file, definitions.front().line);
}

void ScriptEditorTab::BindEvents() {
  editor_->Bind(wxEVT_STC_CHARADDED, &ScriptEditorTab::OnCharAdded, this);
  editor_->Bind(wxEVT_STC_MODIFIED, &ScriptEditorTab::OnModified, this);
  editor_->Bind(wxEVT_STC_UPDATEUI, &ScriptEditorTab::OnUpdateUI, this);
  editor_->Bind(wxEVT_STC_MARGINCLICK, &ScriptEditorTab::OnMarginClick, this);
  editor_->Bind(wxEVT_STC_SAVEPOINTLEFT, [this](wxStyledTextEvent&) {
    if (panel_) panel_->SetTabTitle(this);
  });
  editor_->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent&) {
    if (panel_) panel_->SetTabTitle(this);
  });
  editor_->Bind(wxEVT_STC_AUTOCOMP_SELECTION,
                &ScriptEditorTab::OnAutoCompSelection, this);
  editor_->Bind(wxEVT_KEY_DOWN, &ScriptEditorTab::OnEditorKeyDown, this);
  editor_->Bind(wxEVT_LEFT_DOWN, &ScriptEditorTab::OnMouseDown, this);
}

void ScriptEditorTab::OnCharAdded(wxStyledTextEvent& evt) {
  const int key = evt.GetKey();
  const int pos = editor_->GetCurrentPos();
  if (key == '\n') {
    AutoIndent(pos);
  } else if (key == '}' && panel_ && !vim_enabled_) {
    const int line = editor_->GetCurrentLine();
    wxString current_line = editor_->GetLine(line);
    wxString current = current_line.Trim();
    if (current == "}") {
      const int indent = editor_->GetLineIndentation(line);
      if (indent >= 4) editor_->SetLineIndentation(line, indent - 4);
    }
  }
  if (key == '(') {
    ShowCallTip();
  }
  if (IsInCode(pos - 1)) {
    if (key == '.' || key == ':' ||
        std::isalnum(static_cast<unsigned char>(key)) || key == '_') {
      ShowCompletion(false);
    }
  }
  if (panel_) panel_->RequestLint();
  evt.Skip();
}

void ScriptEditorTab::OnModified(wxStyledTextEvent& evt) {
  const int type = evt.GetModificationType();
  if (type & (wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT)) {
    if (panel_) {
      panel_->TabModified(this);
      panel_->RequestLint();
    }
    UpdateLineNumberMargin();
    if (panel_) panel_->SetTabTitle(this);
  }
  evt.Skip();
}

void ScriptEditorTab::OnUpdateUI(wxStyledTextEvent& evt) {
  if (panel_) panel_->TabCursorMoved(this);
  const int pos = editor_->GetCurrentPos();
  const int match = editor_->BraceMatch(pos);
  if (match >= 0) {
    editor_->BraceHighlight(pos, match);
  } else {
    editor_->BraceHighlight(-1, -1);
  }
  evt.Skip();
}

void ScriptEditorTab::OnMarginClick(wxStyledTextEvent& evt) {
  if (evt.GetMargin() == 2) {
    const int line = editor_->LineFromPosition(evt.GetPosition());
    editor_->ToggleFold(line);
  }
  evt.Skip();
}

void ScriptEditorTab::OnAutoCompSelection(wxStyledTextEvent& evt) {
  if (!panel_) return;
  const std::string label = evt.GetString().utf8_str().data();
  auto it = completion_details_.find(label);
  if (it != completion_details_.end() && !it->second.empty()) {
    panel_->SetStatusMessage(wxString::FromUTF8(label + "  " + it->second));
  }
  evt.Skip();
}

void ScriptEditorTab::OnEditorKeyDown(wxKeyEvent& evt) {
  if (vim_ && vim_->OnKeyDown(evt)) return;
  const int code = evt.GetKeyCode();
  const bool ctrl = evt.ControlDown() || evt.CmdDown();
  if (ctrl && (code == 'F' || code == 'f')) {
    if (panel_) panel_->ShowFindBar(false);
    return;
  }
  if (ctrl && (code == 'H' || code == 'h')) {
    if (panel_) panel_->ShowFindBar(true);
    return;
  }
  if (ctrl && (code == 'S' || code == 's')) {
    if (panel_) panel_->SaveActive();
    return;
  }
  if (ctrl && (code == 'W' || code == 'w')) {
    if (panel_) panel_->TabCloseRequested(this, false);
    return;
  }
  if (ctrl && evt.ShiftDown() && (code == 'O' || code == 'o')) {
    if (panel_) panel_->ShowGoToMethod();
    return;
  }
  if (code == WXK_SPACE && evt.ControlDown()) {
    ShowCompletion(true);
    return;
  }
  if (code == WXK_F12) {
    GoToDefinition(editor_->GetCurrentPos());
    return;
  }
  evt.Skip();
}

void ScriptEditorTab::OnMouseDown(wxMouseEvent& evt) {
  if (evt.ControlDown() || evt.CmdDown()) {
    GoToDefinition(editor_->PositionFromPoint(evt.GetPosition()));
    return;
  }
  evt.Skip();
}

void ScriptEditorTab::AutoIndent(int pos) {
  const int line = editor_->GetCurrentLine();
  if (line <= 0) return;
  int indent = editor_->GetLineIndentation(line - 1);
  wxString previous_line = editor_->GetLine(line - 1);
  wxString previous = previous_line.Trim();
  if (previous.EndsWith("{")) indent += 4;
  wxString current_line = editor_->GetLine(line);
  wxString current = current_line.Trim();
  if (current.StartsWith("}") && indent >= 4) indent -= 4;
  editor_->SetLineIndentation(line, std::max(0, indent));
  editor_->GotoPos(
      std::min(editor_->GetLineIndentPosition(line), editor_->GetTextLength()));
  (void)pos;
}

}  // namespace Mercury::EditorUI
