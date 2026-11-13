/*
 * script_language.cc
 */
#include "script_language.h"

#include <angelscript.h>

#include <algorithm>
#include <string_view>
#include <unordered_set>

#include "scriptbuilder/scriptbuilder.h"
#include "scripting/as/script_api.h"

namespace Mercury::EditorUI {

namespace {

std::string TypeDeclaration(asIScriptEngine* engine, int type_id) {
  if (!engine || type_id == asTYPEID_VOID) return "void";
  const char* decl = engine->GetTypeDeclaration(type_id, true);
  return decl ? decl : "";
}

std::string FunctionNamespace(asIScriptFunction* function) {
  const char* ns = function ? function->GetNamespace() : nullptr;
  return ns ? ns : "";
}

bool EndsWithProperty(const char* declaration) {
  if (!declaration) return false;
  const std::string_view text(declaration);
  return text.size() >= 9 && text.substr(text.size() - 9) == " property";
}

}  // namespace

ScriptLanguageService::~ScriptLanguageService() {
  if (engine_) {
    engine_->ShutDownAndRelease();
    engine_ = nullptr;
  }
}

bool ScriptLanguageService::Initialize() {
  if (engine_) return true;
  error_.clear();
  engine_ = asCreateScriptEngine();
  if (!engine_) {
    error_ = "Could not create the AngelScript engine.";
    return false;
  }
  engine_->SetMessageCallback(asFUNCTION(MessageCallback), this, asCALL_CDECL);
  engine_->SetEngineProperty(asEP_SCRIPT_SCANNER, 1);  // UTF-8
  engine_->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 0);
  engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 3);
  ConfigureScriptEngine(engine_);
  BuildNativeIndex();
  return true;
}

void ScriptLanguageService::MessageCallback(const asSMessageInfo* message,
                                            void* param) {
  auto* self = static_cast<ScriptLanguageService*>(param);
  if (!self || !self->collecting_ || !message) return;
  if (message->type == asMSGTYPE_INFORMATION) return;
  ScriptDiagnostic diagnostic;
  diagnostic.file = message->section ? message->section : "";
  diagnostic.line = message->row > 0 ? message->row : 0;
  diagnostic.column = message->col > 0 ? message->col : 0;
  diagnostic.warning = message->type == asMSGTYPE_WARNING;
  diagnostic.message = message->message ? message->message : "";
  self->collecting_->push_back(std::move(diagnostic));
}

bool ScriptLanguageService::Check(
    const std::vector<std::pair<std::string, std::string>>& sections,
    std::vector<ScriptDiagnostic>* diagnostics) {
  if (!Initialize()) return false;
  if (diagnostics) diagnostics->clear();

  const std::string module_name =
      "editor_check_" + std::to_string(++module_counter_);
  CScriptBuilder builder;
  if (builder.StartNewModule(engine_, module_name.c_str()) < 0) {
    error_ = "Could not start an AngelScript module.";
    return false;
  }
  builder.AddSectionFromMemory("<engine>", ApiPreludeSource());
  for (const auto& [name, text] : sections) {
    builder.AddSectionFromMemory(name.c_str(), text.c_str());
  }

  std::vector<ScriptDiagnostic> collected;
  collecting_ = &collected;
  builder.BuildModule();
  collecting_ = nullptr;
  engine_->DiscardModule(module_name.c_str());

  if (diagnostics) *diagnostics = std::move(collected);
  return true;
}

void ScriptLanguageService::AddNativeTypeKeywords(const std::string& name) {
  if (name.empty()) return;
  if (!type_keywords_.empty()) type_keywords_.push_back(' ');
  type_keywords_ += name;
}

void ScriptLanguageService::BuildNativeIndex() {
  native_.clear();
  type_keywords_.clear();
  if (!engine_) return;

  auto add = [&](ScriptSymbol symbol) {
    if (symbol.name.empty()) return;
    native_.push_back(std::move(symbol));
  };

  for (asUINT i = 0; i < engine_->GetGlobalFunctionCount(); ++i) {
    asIScriptFunction* function = engine_->GetGlobalFunctionByIndex(i);
    if (!function) continue;
    ScriptSymbol symbol;
    symbol.kind = ScriptSymbolKind::kFunction;
    symbol.name = function->GetName();
    symbol.owner = FunctionNamespace(function);
    symbol.type = TypeDeclaration(engine_, function->GetReturnTypeId());
    const char* decl = function->GetDeclaration(false, true, true);
    symbol.signature = decl ? decl : symbol.type + " " + symbol.name;
    add(std::move(symbol));
  }

  for (asUINT i = 0; i < engine_->GetGlobalPropertyCount(); ++i) {
    const char* name = nullptr;
    const char* ns = nullptr;
    int type_id = 0;
    bool is_const = false;
    if (engine_->GetGlobalPropertyByIndex(i, &name, &ns, &type_id, &is_const) <
            0 ||
        !name) {
      continue;
    }
    ScriptSymbol symbol;
    symbol.kind = ScriptSymbolKind::kGlobal;
    symbol.name = name;
    symbol.owner = ns ? ns : "";
    symbol.type = TypeDeclaration(engine_, type_id);
    if (is_const) symbol.type = "const " + symbol.type;
    symbol.signature = symbol.type + " " + symbol.name;
    add(std::move(symbol));
  }

  for (asUINT i = 0; i < engine_->GetObjectTypeCount(); ++i) {
    asITypeInfo* type = engine_->GetObjectTypeByIndex(i);
    if (!type) continue;
    const char* type_name = type->GetName();
    if (!type_name || !*type_name) continue;
    const std::string owner = type_name;
    AddNativeTypeKeywords(owner);

    ScriptSymbol type_symbol;
    type_symbol.kind = ScriptSymbolKind::kClass;
    type_symbol.name = owner;
    type_symbol.type = owner;
    type_symbol.signature = "class " + owner;
    add(std::move(type_symbol));

    std::unordered_set<std::string> property_names;
    for (asUINT m = 0; m < type->GetMethodCount(); ++m) {
      asIScriptFunction* method = type->GetMethodByIndex(m, true);
      if (!method) continue;
      const std::string method_name =
          method->GetName() ? method->GetName() : "";
      if (method_name.empty()) continue;
      const char* declaration = method->GetDeclaration(false, true, true);
      if ((method->IsProperty() || EndsWithProperty(declaration)) &&
          (method_name.starts_with("get_") ||
           method_name.starts_with("set_"))) {
        const std::string prop = method_name.substr(4);
        if (prop.empty() || !property_names.insert(prop).second) continue;
        ScriptSymbol field;
        field.kind = ScriptSymbolKind::kField;
        field.name = prop;
        field.owner = owner;
        field.property = true;
        if (method_name.starts_with("get_")) {
          field.type = TypeDeclaration(engine_, method->GetReturnTypeId());
        } else if (method->GetParamCount() > 0) {
          int param_type = 0;
          method->GetParam(0, &param_type);
          field.type = TypeDeclaration(engine_, param_type);
        } else {
          field.type = "void";
        }
        field.signature = field.type + " " + prop;
        add(std::move(field));
        continue;
      }
      ScriptSymbol symbol;
      symbol.kind = ScriptSymbolKind::kMethod;
      symbol.name = method_name;
      symbol.owner = owner;
      symbol.type = TypeDeclaration(engine_, method->GetReturnTypeId());
      symbol.signature =
          declaration ? declaration : symbol.type + " " + method_name + "()";
      add(std::move(symbol));
    }

    for (asUINT p = 0; p < type->GetPropertyCount(); ++p) {
      const char* property_name = nullptr;
      int property_type = 0;
      if (type->GetProperty(p, &property_name, &property_type) < 0 ||
          !property_name) {
        continue;
      }
      ScriptSymbol field;
      field.kind = ScriptSymbolKind::kField;
      field.name = property_name;
      field.owner = owner;
      field.type = TypeDeclaration(engine_, property_type);
      field.signature = field.type + " " + field.name;
      add(std::move(field));
    }

    for (asUINT f = 0; f < type->GetFactoryCount(); ++f) {
      asIScriptFunction* factory = type->GetFactoryByIndex(f);
      if (!factory) continue;
      ScriptSymbol symbol;
      symbol.kind = ScriptSymbolKind::kFunction;
      symbol.name = owner;
      symbol.type = owner;
      const char* declaration = factory->GetDeclaration(false, true, true);
      symbol.signature = declaration ? declaration : owner + " " + owner;
      add(std::move(symbol));
    }
  }

  for (asUINT i = 0; i < engine_->GetEnumCount(); ++i) {
    asITypeInfo* type = engine_->GetEnumByIndex(i);
    if (!type || !type->GetName()) continue;
    const std::string enum_name = type->GetName();
    AddNativeTypeKeywords(enum_name);
    ScriptSymbol enum_symbol;
    enum_symbol.kind = ScriptSymbolKind::kEnum;
    enum_symbol.name = enum_name;
    enum_symbol.type = enum_name;
    enum_symbol.signature = "enum " + enum_name;
    add(std::move(enum_symbol));
    for (asUINT v = 0; v < type->GetEnumValueCount(); ++v) {
      asINT64 enum_value = 0;
      const char* value = type->GetEnumValueByIndex(v, &enum_value);
      if (!value) continue;
      ScriptSymbol symbol;
      symbol.kind = ScriptSymbolKind::kEnumValue;
      symbol.name = value;
      symbol.owner = enum_name;
      symbol.type = enum_name;
      symbol.signature = enum_name + "::" + value;
      add(std::move(symbol));
    }
  }

  for (asUINT i = 0; i < engine_->GetTypedefCount(); ++i) {
    asITypeInfo* type = engine_->GetTypedefByIndex(i);
    if (!type || !type->GetName()) continue;
    ScriptSymbol symbol;
    symbol.kind = ScriptSymbolKind::kTypedef;
    symbol.name = type->GetName();
    symbol.type = TypeDeclaration(engine_, type->GetTypeId());
    symbol.signature = "typedef " + symbol.name;
    add(std::move(symbol));
  }

  for (asUINT i = 0; i < engine_->GetFuncdefCount(); ++i) {
    asITypeInfo* type = engine_->GetFuncdefByIndex(i);
    if (!type || !type->GetName()) continue;
    ScriptSymbol symbol;
    symbol.kind = ScriptSymbolKind::kFuncdef;
    symbol.name = type->GetName();
    asIScriptFunction* signature = type->GetFuncdefSignature();
    const char* declaration =
        signature ? signature->GetDeclaration(true, true, true) : nullptr;
    symbol.signature = declaration ? declaration : "funcdef " + symbol.name;
    add(std::move(symbol));
  }

  // Stable order makes duplicate filtering predictable in the editor.
  std::sort(native_.begin(), native_.end(), [](const auto& a, const auto& b) {
    if (a.name != b.name) return a.name < b.name;
    return a.owner < b.owner;
  });
  if (!type_keywords_.empty()) {
    type_keywords_ += " string array dictionary";
  }
}

}  // namespace Mercury::EditorUI
