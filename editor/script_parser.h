/*
 * script_parser.h
 *
 * A small, tolerant AngelScript parser used by the built-in script editor.
 *
 * It is deliberately not a compiler: it extracts declarations and local
 * variable types so completion and the outline work while a file is being
 * edited. The real AngelScript compiler remains the source of truth for
 * diagnostics (see script_language.h).
 */
#ifndef MERCURY_EDITOR_SCRIPT_PARSER_H
#define MERCURY_EDITOR_SCRIPT_PARSER_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Mercury::EditorUI {

enum class ScriptSymbolKind {
  kClass,
  kEnum,
  kEnumValue,
  kFunction,
  kMethod,
  kField,
  kGlobal,
  kNamespace,
  kFuncdef,
  kTypedef,
};

struct ScriptSymbol {
  ScriptSymbolKind kind = ScriptSymbolKind::kGlobal;
  std::string name;
  // Class for methods/fields, namespace for namespace members, enum for enum
  // values. Empty for top-level symbols.
  std::string owner;
  // Return type for functions, field type for fields.
  std::string type;
  // Human-readable declaration, e.g. "void onTick(double dt)".
  std::string signature;
  // Project-relative section name, e.g. "scripts/ball.as". Empty for native
  // engine symbols.
  std::string file;
  int line = 0;    // 1-based
  int column = 0;  // 1-based
  bool property = false;
};

struct ScriptToken {
  enum class Kind { kIdentifier, kNumber, kString, kPunct };
  Kind kind = Kind::kPunct;
  std::string text;
  std::size_t begin = 0;
  std::size_t end = 0;
  int line = 1;
  int column = 1;
};

struct ScriptClassInfo {
  std::string name;
  std::string base;
  std::size_t open_brace = 0;
  std::size_t close_brace = 0;
  int line = 0;
};

struct ScriptFunctionInfo {
  std::string name;
  std::string return_type;
  std::string owner;
  std::vector<std::pair<std::string, std::string>> params;  // name, type
  std::size_t body_open = 0;
  std::size_t body_close = 0;
  int line = 0;
};

struct ParsedScript {
  std::string res_path;
  std::string text;
  std::vector<ScriptToken> tokens;
  std::vector<ScriptSymbol> symbols;
  std::vector<ScriptClassInfo> classes;
  std::vector<ScriptFunctionInfo> functions;
  std::vector<std::pair<std::string, std::string>> globals;  // name, type

  const ScriptClassInfo* ClassAt(std::size_t offset) const;
  const ScriptFunctionInfo* FunctionAt(std::size_t offset) const;
};

struct CompletionItem {
  std::string label;
  std::string detail;
  int priority = 0;  // lower sorts first before Scintilla filters
};

struct ScriptLocation {
  std::string file;
  int line = 0;
  int column = 0;
};

// Parses one file. `res_path` is the project-relative section name used in
// diagnostics (e.g. "scripts/ball.as").
ParsedScript ParseScript(const std::string& res_path, const std::string& text);

// Cache of parsed project files, keyed by res_path.
class ScriptWorkspace {
 public:
  void SetFile(const std::string& res_path, const std::string& text);
  void RemoveFile(const std::string& res_path);
  void Clear();
  const ParsedScript* Find(std::string_view res_path) const;
  const std::vector<ParsedScript>& Files() const { return files_; }

  std::vector<const ScriptSymbol*> FindSymbolsByName(
      std::string_view name) const;

 private:
  std::vector<ParsedScript> files_;
  std::unordered_map<std::string, std::size_t> index_;
};

// Completes at `offset` in the live buffer `text`. `workspace` supplies the
// other project files; `native_symbols` comes from the engine API.
std::vector<CompletionItem> CompleteAt(
    const ScriptWorkspace& workspace, const std::string& res_path,
    const std::string& text, std::size_t offset,
    const std::vector<ScriptSymbol>& native_symbols);

// Finds definitions of the identifier at `offset` in the live buffer.
std::vector<ScriptLocation> FindDefinitionsAt(
    const ScriptWorkspace& workspace, const std::string& res_path,
    const std::string& text, std::size_t offset,
    const std::vector<ScriptSymbol>& native_symbols);

// Best-effort return type of the expression immediately before a `.`.
std::string InferExpressionType(const ParsedScript& parsed, std::size_t dot);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCRIPT_PARSER_H
