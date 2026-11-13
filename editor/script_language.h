/*
 * script_language.h
 *
 * Editor-side AngelScript language service: a private script engine used to
 * enumerate the native API for completion and to compile project scripts for
 * real diagnostics without touching the game runtime.
 */
#ifndef MERCURY_EDITOR_SCRIPT_LANGUAGE_H
#define MERCURY_EDITOR_SCRIPT_LANGUAGE_H

#include <string>
#include <utility>
#include <vector>

#include "script_parser.h"

class asIScriptEngine;
struct asSMessageInfo;

namespace Mercury::EditorUI {

struct ScriptDiagnostic {
  std::string file;  // project-relative section, e.g. "scripts/ball.as"
  int line = 0;      // 1-based
  int column = 0;    // 1-based
  bool warning = false;
  std::string message;
};

class ScriptLanguageService {
 public:
  ScriptLanguageService() = default;
  ~ScriptLanguageService();

  ScriptLanguageService(const ScriptLanguageService&) = delete;
  ScriptLanguageService& operator=(const ScriptLanguageService&) = delete;

  bool Initialize();
  bool IsInitialized() const { return engine_ != nullptr; }
  const std::string& Error() const { return error_; }

  // Compiles `sections` (name, source) as one AngelScript module and collects
  // diagnostics. The module is discarded afterwards; no script types are
  // registered.
  bool Check(const std::vector<std::pair<std::string, std::string>>& sections,
             std::vector<ScriptDiagnostic>* diagnostics);

  const std::vector<ScriptSymbol>& NativeSymbols() const { return native_; }
  const std::string& TypeKeywords() const { return type_keywords_; }

 private:
  static void MessageCallback(const asSMessageInfo* message, void* param);
  void BuildNativeIndex();
  void AddNativeTypeKeywords(const std::string& name);

  asIScriptEngine* engine_ = nullptr;
  std::vector<ScriptDiagnostic>* collecting_ = nullptr;
  std::vector<ScriptSymbol> native_;
  std::string type_keywords_;
  std::string error_;
  int module_counter_ = 0;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_SCRIPT_LANGUAGE_H
