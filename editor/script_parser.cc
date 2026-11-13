/*
 * script_parser.cc
 */
#include "script_parser.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_set>

namespace Mercury::EditorUI {

namespace {

bool IsIdentifierStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsIdentifierChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool IsKeyword(std::string_view text) {
  static const std::unordered_set<std::string> kKeywords = {
      "abstract", "auto",      "bool",     "break",     "case",      "cast",
      "class",    "const",     "continue", "default",   "do",        "double",
      "else",     "enum",      "explicit", "external",  "false",     "final",
      "float",    "for",       "from",     "funcdef",   "if",        "import",
      "in",       "inout",     "int",      "int8",      "int16",     "int32",
      "int64",    "interface", "is",       "mixin",     "namespace", "null",
      "out",      "override",  "private",  "protected", "return",    "shared",
      "string",   "super",     "switch",   "this",      "true",      "typedef",
      "uint",     "uint8",     "uint16",   "uint32",    "uint64",    "using",
      "void",     "while",     "property", "new",       "delete",    "explicit",
  };
  return kKeywords.contains(std::string(text));
}

bool IsAccessKeyword(std::string_view text) {
  return text == "private" || text == "protected" || text == "public";
}

bool IsTypeModifier(std::string_view text) {
  return text == "const" || text == "in" || text == "out" || text == "inout" ||
         text == "in_out";
}

std::vector<ScriptToken> Tokenize(const std::string& text) {
  std::vector<ScriptToken> tokens;
  size_t i = 0;
  int line = 1;
  int column = 1;
  auto advance = [&](char c) {
    if (c == '\n') {
      ++line;
      column = 1;
    } else {
      ++column;
    }
  };
  while (i < text.size()) {
    const char c = text[i];
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\v' ||
        c == '\f') {
      advance(c);
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      while (i < text.size() && text[i] != '\n') {
        advance(text[i]);
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      advance(text[i++]);
      advance(text[i++]);
      while (i < text.size()) {
        if (text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/') {
          advance(text[i++]);
          advance(text[i++]);
          break;
        }
        advance(text[i++]);
      }
      continue;
    }
    ScriptToken token;
    token.begin = i;
    token.line = line;
    token.column = column;
    if (IsIdentifierStart(c)) {
      token.kind = ScriptToken::Kind::kIdentifier;
      while (i < text.size() && IsIdentifierChar(text[i])) {
        token.text.push_back(text[i]);
        advance(text[i++]);
      }
    } else if (std::isdigit(static_cast<unsigned char>(c)) ||
               (c == '.' && i + 1 < text.size() &&
                std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
      token.kind = ScriptToken::Kind::kNumber;
      while (i < text.size()) {
        const char n = text[i];
        if (std::isalnum(static_cast<unsigned char>(n)) || n == '.' ||
            n == '_') {
          token.text.push_back(n);
          advance(text[i++]);
        } else {
          break;
        }
      }
    } else if (c == '"' || c == '\'') {
      const char quote = c;
      token.kind = ScriptToken::Kind::kString;
      token.text.push_back(text[i]);
      advance(text[i++]);
      while (i < text.size()) {
        const char n = text[i];
        token.text.push_back(n);
        advance(text[i++]);
        if (n == '\\' && i < text.size()) {
          token.text.push_back(text[i]);
          advance(text[i++]);
        } else if (n == quote) {
          break;
        }
      }
    } else if (c == ':' && i + 1 < text.size() && text[i + 1] == ':') {
      token.kind = ScriptToken::Kind::kPunct;
      token.text = "::";
      advance(text[i++]);
      advance(text[i++]);
    } else if (c == '-' && i + 1 < text.size() && text[i + 1] == '>') {
      token.kind = ScriptToken::Kind::kPunct;
      token.text = "->";
      advance(text[i++]);
      advance(text[i++]);
    } else {
      token.kind = ScriptToken::Kind::kPunct;
      token.text.assign(1, c);
      advance(text[i++]);
    }
    token.end = i;
    tokens.push_back(std::move(token));
  }
  return tokens;
}

std::string JoinTokens(const std::vector<ScriptToken>& tokens, size_t begin,
                       size_t end) {
  std::string out;
  for (size_t i = begin; i < end && i < tokens.size(); ++i) {
    const std::string& text = tokens[i].text;
    if (text.empty()) continue;
    const bool no_space_before =
        text == "::" || text == ")" || text == "]" || text == ">" ||
        text == "," || text == ";" || text == "." || text == "@" ||
        text == "&" || text == "*" || text == "(" || text == "[" || text == "<";
    const bool no_space_after_prev =
        !out.empty() &&
        (out.back() == '(' || out.back() == '[' || out.back() == '<' ||
         out.back() == '@' || out.back() == '&' || out.back() == '*' ||
         out.back() == ':' || out.back() == '.' || out.back() == '!');
    if (!out.empty() && !no_space_before && !no_space_after_prev) {
      out.push_back(' ');
    }
    out += text;
  }
  return out;
}

std::string NormalizeType(std::string type) {
  auto trim = [](std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
      s.pop_back();
  };
  trim(type);
  while (type.starts_with("const ")) type.erase(0, 6);
  while (!type.empty() && (type.back() == '@' || type.back() == '&' ||
                           type.back() == '*' || type.back() == ' ')) {
    type.pop_back();
  }
  trim(type);
  if (const size_t generic = type.find('<'); generic != std::string::npos) {
    type.resize(generic);
  }
  if (type.ends_with("[]")) type.resize(type.size() - 2);
  trim(type);
  return type;
}

class Parser {
 public:
  Parser(ParsedScript& parsed) : parsed_(parsed), tokens_(parsed.tokens) {}

  void Run() {
    size_t i = 0;
    ParseScope(i, tokens_.size(), "");
  }

 private:
  const ScriptToken& Tok(size_t i) const { return tokens_[i]; }

  bool Is(size_t i, std::string_view text) const {
    return i < tokens_.size() && tokens_[i].text == text;
  }

  size_t Match(size_t open, std::string_view open_text,
               std::string_view close_text) const {
    int depth = 0;
    for (size_t i = open; i < tokens_.size(); ++i) {
      if (tokens_[i].text == open_text) {
        ++depth;
      } else if (tokens_[i].text == close_text) {
        --depth;
        if (depth == 0) return i;
      }
    }
    return tokens_.size();
  }

  size_t SkipMetadata(size_t i) const {
    if (!Is(i, "[")) return i;
    return Match(i, "[", "]") + 1;
  }

  void ParseScope(size_t& i, size_t end, const std::string& ns) {
    while (i < end && i < tokens_.size()) {
      if (Is(i, "}")) {
        ++i;
        return;
      }
      if (Is(i, ";")) {
        ++i;
        continue;
      }
      if (Is(i, "[")) {
        i = SkipMetadata(i);
        continue;
      }
      if (Is(i, "namespace")) {
        ParseNamespace(i, end, ns);
        continue;
      }
      if (Is(i, "class") || Is(i, "interface") || Is(i, "mixin")) {
        ParseClass(i, end, ns);
        continue;
      }
      if (Is(i, "enum")) {
        ParseEnum(i, end, ns);
        continue;
      }
      if (Is(i, "funcdef")) {
        ParseFuncdef(i, end, ns);
        continue;
      }
      if (Is(i, "typedef")) {
        ParseTypedef(i, end, ns);
        continue;
      }
      if (IsAccessKeyword(Tok(i).text) || IsTypeModifier(Tok(i).text)) {
        ++i;
        continue;
      }
      if (!ParseDeclaration(i, end, ns, false)) {
        ++i;
      }
    }
  }

  void ParseNamespace(size_t& i, size_t end, const std::string& ns) {
    const size_t start = i;
    ++i;  // namespace
    if (i >= end || Tok(i).kind != ScriptToken::Kind::kIdentifier) {
      i = start + 1;
      return;
    }
    const std::string name = Tok(i).text;
    ++i;
    if (!Is(i, "{")) {
      i = start + 1;
      return;
    }
    const std::string full = ns.empty() ? name : ns + "::" + name;
    parsed_.symbols.push_back({ScriptSymbolKind::kNamespace,
                               name,
                               ns,
                               {},
                               "namespace " + name,
                               parsed_.res_path,
                               Tok(start).line,
                               Tok(start).column});
    ++i;  // {
    ParseScope(i, end, full);
  }

  void ParseClass(size_t& i, size_t end, const std::string& ns) {
    const size_t start = i;
    ++i;  // class/interface/mixin
    if (i >= end || Tok(i).kind != ScriptToken::Kind::kIdentifier) {
      i = start + 1;
      return;
    }
    ScriptSymbol symbol;
    symbol.kind = ScriptSymbolKind::kClass;
    symbol.name = Tok(i).text;
    symbol.owner = ns;
    symbol.file = parsed_.res_path;
    symbol.line = Tok(i).line;
    symbol.column = Tok(i).column;
    ++i;
    std::string base;
    if (Is(i, ":")) {
      ++i;
      const size_t base_begin = i;
      while (i < end && !Is(i, "{")) ++i;
      base = JoinTokens(tokens_, base_begin, i);
      symbol.signature = "class " + symbol.name;
      if (!base.empty()) symbol.signature += " : " + base;
    } else {
      symbol.signature = "class " + symbol.name;
    }
    parsed_.symbols.push_back(symbol);
    ScriptClassInfo info;
    info.name = symbol.name;
    info.base = base;
    info.line = symbol.line;
    if (i >= end || !Is(i, "{")) {
      // Forward declaration.
      parsed_.classes.push_back(info);
      return;
    }
    info.open_brace = Tok(i).begin;
    const size_t close = Match(i, "{", "}");
    info.close_brace =
        close < tokens_.size() ? Tok(close).begin : parsed_.text.size();
    parsed_.classes.push_back(info);

    ++i;  // {
    std::unordered_set<std::string> property_seen;
    while (i < end && i < tokens_.size() && !Is(i, "}")) {
      if (Is(i, ";")) {
        ++i;
        continue;
      }
      if (Is(i, "[")) {
        i = SkipMetadata(i);
        continue;
      }
      if (Is(i, "class") || Is(i, "interface")) {
        ParseClass(i, end, ns);
        continue;
      }
      if (Is(i, "enum")) {
        ParseEnum(i, end, info.name);
        continue;
      }
      if (IsAccessKeyword(Tok(i).text)) {
        ++i;
        continue;
      }
      if (!ParseDeclaration(i, end, info.name, true, &property_seen)) {
        ++i;
      }
    }
    if (i < tokens_.size() && Is(i, "}")) ++i;
  }

  void ParseEnum(size_t& i, size_t end, const std::string& ns) {
    const size_t start = i;
    ++i;  // enum
    if (i >= end || Tok(i).kind != ScriptToken::Kind::kIdentifier) {
      i = start + 1;
      return;
    }
    const std::string name = Tok(i).text;
    const int line = Tok(i).line;
    const int column = Tok(i).column;
    ++i;
    if (Is(i, ":")) {
      while (i < end && !Is(i, "{")) ++i;
    }
    if (!Is(i, "{")) {
      i = start + 1;
      return;
    }
    parsed_.symbols.push_back({ScriptSymbolKind::kEnum, name, ns, name,
                               "enum " + name, parsed_.res_path, line, column});
    const size_t close = Match(i, "{", "}");
    ++i;
    while (i < close && i < tokens_.size()) {
      if (Tok(i).kind == ScriptToken::Kind::kIdentifier &&
          !IsKeyword(Tok(i).text)) {
        parsed_.symbols.push_back({ScriptSymbolKind::kEnumValue, Tok(i).text,
                                   name, name, Tok(i).text, parsed_.res_path,
                                   Tok(i).line, Tok(i).column});
        ++i;
        while (i < close && !Is(i, ",")) ++i;
        if (i < close) ++i;
      } else {
        ++i;
      }
    }
    i = close < tokens_.size() ? close + 1 : tokens_.size();
  }

  void ParseFuncdef(size_t& i, size_t end, const std::string& ns) {
    const size_t start = i;
    ++i;
    size_t open = i;
    while (open < end && !Is(open, "(")) ++open;
    if (open >= end || open == i) {
      i = start + 1;
      return;
    }
    std::string name;
    size_t name_index = open;
    for (size_t k = open; k > i; --k) {
      if (tokens_[k - 1].kind == ScriptToken::Kind::kIdentifier) {
        name_index = k - 1;
        name = tokens_[k - 1].text;
        break;
      }
    }
    if (name.empty()) {
      i = start + 1;
      return;
    }
    const size_t close = Match(open, "(", ")");
    parsed_.symbols.push_back(
        {ScriptSymbolKind::kFuncdef, name, ns,
         JoinTokens(tokens_, i, name_index),
         JoinTokens(tokens_, start, std::min(close + 1, tokens_.size())),
         parsed_.res_path, tokens_[name_index].line,
         tokens_[name_index].column});
    i = close < tokens_.size() ? close + 1 : tokens_.size();
    if (Is(i, ";")) ++i;
  }

  void ParseTypedef(size_t& i, size_t end, const std::string& ns) {
    const size_t start = i;
    size_t semi = i;
    while (semi < end && !Is(semi, ";")) ++semi;
    if (semi >= end) {
      i = start + 1;
      return;
    }
    size_t name_index = semi;
    for (size_t k = semi; k > start; --k) {
      if (tokens_[k - 1].kind == ScriptToken::Kind::kIdentifier) {
        name_index = k - 1;
        break;
      }
    }
    if (name_index < semi) {
      parsed_.symbols.push_back(
          {ScriptSymbolKind::kTypedef, tokens_[name_index].text, ns,
           JoinTokens(tokens_, start + 1, name_index),
           JoinTokens(tokens_, start, semi), parsed_.res_path,
           tokens_[name_index].line, tokens_[name_index].column});
    }
    i = semi + 1;
  }

  // Parses a function or variable declaration starting at `i`. Returns false
  // (without consuming) when the tokens do not look like a declaration.
  bool ParseDeclaration(
      size_t& i, size_t end, const std::string& owner, bool class_member,
      std::unordered_set<std::string>* property_seen = nullptr) {
    const size_t start = i;
    // Find the first declaration terminator while tracking groups.
    int paren = 0;
    int angle = 0;
    int bracket = 0;
    size_t term = start;
    for (; term < end && term < tokens_.size(); ++term) {
      const std::string& t = tokens_[term].text;
      if (t == "(") {
        if (paren == 0 && angle == 0 && bracket == 0) break;
        ++paren;
      } else if (t == ")") {
        if (paren > 0) --paren;
      } else if (t == "<") {
        ++angle;
      } else if (t == ">") {
        if (angle > 0) --angle;
      } else if (t == "[") {
        ++bracket;
      } else if (t == "]") {
        if (bracket > 0) --bracket;
      } else if (paren == 0 && angle == 0 && bracket == 0 &&
                 (t == "=" || t == ";" || t == "," || t == "{" || t == "}")) {
        break;
      }
    }
    if (term >= end || term >= tokens_.size()) {
      i = start + 1;
      return false;
    }

    const bool is_function = Is(term, "(");
    size_t name_index = term;
    for (size_t k = term; k > start; --k) {
      if (tokens_[k - 1].kind == ScriptToken::Kind::kIdentifier) {
        name_index = k - 1;
        break;
      }
    }
    if (name_index >= term) {
      i = start + 1;
      return false;
    }
    // A member access expression is not a declaration.
    if (name_index > start &&
        (Is(name_index - 1, ".") || Is(name_index - 1, "->"))) {
      i = start + 1;
      return false;
    }
    const std::string& name = tokens_[name_index].text;
    if (name.empty() || (IsKeyword(name) && name != "this")) {
      i = start + 1;
      return false;
    }
    std::string type = JoinTokens(tokens_, start, name_index);
    const bool is_ctor = class_member && name == owner && name_index == start;
    const bool is_dtor = class_member && name == owner && name_index > start &&
                         Is(name_index - 1, "~");
    // `const` alone is not a type.
    if (!is_ctor && !is_dtor && NormalizeType(type).empty()) {
      i = start + 1;
      return false;
    }
    if (is_ctor || is_dtor) type.clear();
    // Skip leading modifiers in the displayed type.
    while (type.starts_with("const ")) type.erase(0, 6);

    if (is_function) {
      const size_t close = Match(term, "(", ")");
      std::vector<ScriptFunctionInfo>& functions = parsed_.functions;
      ScriptFunctionInfo function;
      function.name = name;
      function.return_type = type;
      function.owner = owner;
      function.line = tokens_[name_index].line;
      ParseParams(term + 1, close, &function.params);
      size_t after = close < tokens_.size() ? close + 1 : tokens_.size();
      bool is_property = false;
      while (after < end && after < tokens_.size() &&
             (Is(after, "const") || Is(after, "final") ||
              Is(after, "override") || Is(after, "property"))) {
        if (Is(after, "property")) is_property = true;
        ++after;
      }
      function.body_open = 0;
      function.body_close = 0;
      if (after < tokens_.size() && Is(after, "{")) {
        const size_t body_close = Match(after, "{", "}");
        function.body_open = tokens_[after].begin;
        function.body_close = body_close < tokens_.size()
                                  ? tokens_[body_close].begin
                                  : parsed_.text.size();
        functions.push_back(function);
        if (class_member) {
          ScriptSymbol symbol;
          symbol.kind = ScriptSymbolKind::kMethod;
          symbol.name = name;
          symbol.owner = owner;
          symbol.type = type;
          symbol.file = parsed_.res_path;
          symbol.line = tokens_[name_index].line;
          symbol.column = tokens_[name_index].column;
          symbol.property = is_property;
          const size_t sig_end =
              is_property ? std::min(after, tokens_.size()) : close + 1;
          symbol.signature =
              JoinTokens(tokens_, start, std::min(sig_end, tokens_.size()));
          if (is_property && property_seen &&
              (name.starts_with("get_") || name.starts_with("set_"))) {
            const std::string prop = name.substr(4);
            if (property_seen->insert(prop).second) {
              ScriptSymbol field;
              field.kind = ScriptSymbolKind::kField;
              field.name = prop;
              field.owner = owner;
              field.file = parsed_.res_path;
              field.line = symbol.line;
              field.column = symbol.column;
              field.property = true;
              if (name.starts_with("get_")) {
                field.type = type;
              } else if (!function.params.empty()) {
                field.type = function.params.front().second;
              }
              field.signature = field.type + " " + prop;
              parsed_.symbols.push_back(field);
            }
          } else {
            parsed_.symbols.push_back(symbol);
          }
        } else {
          ScriptSymbol symbol;
          symbol.kind = ScriptSymbolKind::kFunction;
          symbol.name = name;
          symbol.owner = owner;
          symbol.type = type;
          symbol.file = parsed_.res_path;
          symbol.line = tokens_[name_index].line;
          symbol.column = tokens_[name_index].column;
          symbol.signature =
              JoinTokens(tokens_, start, std::min(close + 1, tokens_.size()));
          parsed_.symbols.push_back(symbol);
        }
        i = body_close < tokens_.size() ? body_close + 1 : tokens_.size();
        return true;
      }
      // Declaration without a body (native-like import or forward decl).
      if (class_member) {
        ScriptSymbol symbol;
        symbol.kind = ScriptSymbolKind::kMethod;
        symbol.name = name;
        symbol.owner = owner;
        symbol.type = type;
        symbol.file = parsed_.res_path;
        symbol.line = tokens_[name_index].line;
        symbol.column = tokens_[name_index].column;
        symbol.signature =
            JoinTokens(tokens_, start, std::min(close + 1, tokens_.size()));
        parsed_.symbols.push_back(symbol);
      } else {
        ScriptSymbol symbol;
        symbol.kind = ScriptSymbolKind::kFunction;
        symbol.name = name;
        symbol.owner = owner;
        symbol.type = type;
        symbol.file = parsed_.res_path;
        symbol.line = tokens_[name_index].line;
        symbol.column = tokens_[name_index].column;
        symbol.signature =
            JoinTokens(tokens_, start, std::min(close + 1, tokens_.size()));
        parsed_.symbols.push_back(symbol);
      }
      i = after < tokens_.size() ? after + 1 : tokens_.size();
      return true;
    }

    // Variable declaration.
    const bool is_global = !class_member;
    ScriptSymbol symbol;
    symbol.kind =
        class_member ? ScriptSymbolKind::kField : ScriptSymbolKind::kGlobal;
    symbol.name = name;
    symbol.owner = owner;
    symbol.type = type;
    symbol.file = parsed_.res_path;
    symbol.line = tokens_[name_index].line;
    symbol.column = tokens_[name_index].column;
    symbol.signature = JoinTokens(tokens_, start, name_index + 1);
    parsed_.symbols.push_back(symbol);
    if (is_global) parsed_.globals.emplace_back(name, type);

    // Skip the initializer and terminator.
    size_t k = name_index + 1;
    int depth = 0;
    while (k < end && k < tokens_.size()) {
      const std::string& t = tokens_[k].text;
      if (t == "(" || t == "[" || t == "{") {
        ++depth;
      } else if (t == ")" || t == "]" || t == "}") {
        if (depth == 0) break;
        --depth;
      } else if (depth == 0 && (t == ";" || t == ",")) {
        ++k;
        break;
      } else if (depth == 0 && t == "}") {
        break;
      }
      ++k;
    }
    i = k;
    return true;
  }

  void ParseParams(size_t begin, size_t end,
                   std::vector<std::pair<std::string, std::string>>* out) {
    size_t i = begin;
    while (i < end && i < tokens_.size()) {
      size_t comma = i;
      int depth = 0;
      for (; comma < end && comma < tokens_.size(); ++comma) {
        const std::string& t = tokens_[comma].text;
        if (t == "(" || t == "[" || t == "<" || t == "{") ++depth;
        if (t == ")" || t == "]" || t == ">" || t == "}") --depth;
        if (depth == 0 && t == ",") break;
      }
      if (comma > i) {
        size_t name_index = comma;
        for (size_t k = comma; k > i; --k) {
          if (tokens_[k - 1].kind == ScriptToken::Kind::kIdentifier &&
              !IsKeyword(tokens_[k - 1].text)) {
            name_index = k - 1;
            break;
          }
        }
        if (name_index < comma) {
          const std::string pname = tokens_[name_index].text;
          std::string ptype = JoinTokens(tokens_, i, name_index);
          ptype = NormalizeType(ptype);
          if (!ptype.empty() && ptype != "const" && ptype != "in" &&
              ptype != "out") {
            out->emplace_back(pname, ptype);
          }
        }
      }
      i = comma < end ? comma + 1 : end;
    }
  }

  ParsedScript& parsed_;
  const std::vector<ScriptToken>& tokens_;
};

bool TerminatesDeclaration(const std::string& text) {
  return text == "=" || text == ";" || text == "," || text == ")" ||
         text == "]" || text == "}";
}

// Extracts a local declaration starting at `i` without consuming it. Returns
// the terminator index (or `end`) and fills name/type.
bool TryLocalDeclaration(const ParsedScript& parsed, size_t i, size_t end,
                         std::string* name, std::string* type,
                         size_t* terminator) {
  const auto& tokens = parsed.tokens;
  if (i >= end || tokens[i].kind != ScriptToken::Kind::kIdentifier)
    return false;
  if (IsKeyword(tokens[i].text)) return false;
  size_t j = i;
  if (tokens[j].text == "const") ++j;
  size_t name_index = j;
  int angle = 0;
  while (j < end) {
    const std::string& t = tokens[j].text;
    if (t == "<") ++angle;
    if (t == ">") --angle;
    if (angle <= 0 && TerminatesDeclaration(t)) break;
    if (tokens[j].kind == ScriptToken::Kind::kIdentifier && !IsKeyword(t)) {
      name_index = j;
    }
    ++j;
  }
  if (j >= end || j == i) return false;
  if (tokens[j].text == "(") {
    // A call or function declaration, not a local.
    return false;
  }
  if (name_index == i || name_index == j) return false;
  if (name_index > i && (tokens[name_index - 1].text == "." ||
                         tokens[name_index - 1].text == "->")) {
    return false;
  }
  *type = NormalizeType(JoinTokens(tokens, i, name_index));
  if (type->find('.') != std::string::npos ||
      type->find("->") != std::string::npos ||
      type->find('(') != std::string::npos ||
      type->find(')') != std::string::npos) {
    return false;
  }
  *name = tokens[name_index].text;
  *terminator = j;
  return !type->empty();
}

void AddItem(std::vector<CompletionItem>* items,
             std::unordered_map<std::string, size_t>* seen,
             const std::string& label, const std::string& detail,
             int priority) {
  if (label.empty()) return;
  auto it = seen->find(label);
  if (it != seen->end()) {
    if (items->at(it->second).priority > priority) {
      items->at(it->second).priority = priority;
      items->at(it->second).detail = detail;
    }
    return;
  }
  seen->emplace(label, items->size());
  items->push_back({label, detail, priority});
}

const char* KeywordList() {
  static const char* kList =
      "abstract auto bool break case cast class const continue default do "
      "double else enum explicit external false final float for from funcdef "
      "if import in inout int int8 int16 int32 int64 interface is mixin "
      "namespace null out override private protected property return shared "
      "string super switch this true typedef uint uint8 uint16 uint32 uint64 "
      "using void while";
  static std::string list = kList;
  return list.c_str();
}

}  // namespace

const ScriptClassInfo* ParsedScript::ClassAt(std::size_t offset) const {
  const ScriptClassInfo* best = nullptr;
  for (const ScriptClassInfo& info : classes) {
    if (offset >= info.open_brace && offset <= info.close_brace) {
      if (!best || info.open_brace > best->open_brace) best = &info;
    }
  }
  return best;
}

const ScriptFunctionInfo* ParsedScript::FunctionAt(std::size_t offset) const {
  const ScriptFunctionInfo* best = nullptr;
  for (const ScriptFunctionInfo& info : functions) {
    if (info.body_open == 0) continue;
    if (offset >= info.body_open && offset <= info.body_close) {
      if (!best || info.body_open > best->body_open) best = &info;
    }
  }
  return best;
}

ParsedScript ParseScript(const std::string& res_path, const std::string& text) {
  ParsedScript parsed;
  parsed.res_path = res_path;
  parsed.text = text;
  parsed.tokens = Tokenize(text);
  Parser parser(parsed);
  parser.Run();
  return parsed;
}

void ScriptWorkspace::SetFile(const std::string& res_path,
                              const std::string& text) {
  ParsedScript parsed = ParseScript(res_path, text);
  auto it = index_.find(res_path);
  if (it != index_.end()) {
    files_[it->second] = std::move(parsed);
  } else {
    index_.emplace(res_path, files_.size());
    files_.push_back(std::move(parsed));
  }
}

void ScriptWorkspace::RemoveFile(const std::string& res_path) {
  auto it = index_.find(res_path);
  if (it == index_.end()) return;
  const size_t removed = it->second;
  files_.erase(files_.begin() + removed);
  index_.clear();
  for (size_t i = 0; i < files_.size(); ++i) {
    index_[files_[i].res_path] = i;
  }
}

void ScriptWorkspace::Clear() {
  files_.clear();
  index_.clear();
}

const ParsedScript* ScriptWorkspace::Find(std::string_view res_path) const {
  auto it = index_.find(std::string(res_path));
  return it == index_.end() ? nullptr : &files_[it->second];
}

std::vector<const ScriptSymbol*> ScriptWorkspace::FindSymbolsByName(
    std::string_view name) const {
  std::vector<const ScriptSymbol*> found;
  for (const ParsedScript& file : files_) {
    for (const ScriptSymbol& symbol : file.symbols) {
      if (symbol.name == name) found.push_back(&symbol);
    }
  }
  return found;
}

namespace {

std::string LastWord(const ParsedScript& parsed, size_t offset) {
  std::string word;
  size_t best_end = 0;
  for (const ScriptToken& token : parsed.tokens) {
    if (token.kind != ScriptToken::Kind::kIdentifier) continue;
    if (token.begin <= offset && token.end >= offset) return token.text;
    if (token.end > offset) break;
    if (token.end > best_end) {
      best_end = token.end;
      word = token.text;
    }
  }
  return word;
}

std::string ResolveNameType(const ParsedScript& parsed, size_t offset,
                            std::string_view name,
                            const std::vector<ScriptSymbol>& native_symbols) {
  if (name == "this") {
    if (const ScriptClassInfo* cls = parsed.ClassAt(offset)) return cls->name;
    return {};
  }
  // Parameters of the enclosing function.
  if (const ScriptFunctionInfo* fn = parsed.FunctionAt(offset)) {
    for (const auto& [param, type] : fn->params) {
      if (param == name) return NormalizeType(type);
    }
  }
  // Walk backwards over local declarations, so the nearest one wins.
  if (const ScriptFunctionInfo* fn = parsed.FunctionAt(offset)) {
    std::string result;
    size_t i = 0;
    while (i < parsed.tokens.size() && parsed.tokens[i].begin < fn->body_open)
      ++i;
    while (i < parsed.tokens.size() && parsed.tokens[i].begin < offset) {
      std::string local_name;
      std::string local_type;
      size_t term = i;
      if (TryLocalDeclaration(parsed, i, parsed.tokens.size(), &local_name,
                              &local_type, &term)) {
        if (local_name == name) result = local_type;
        i = term < parsed.tokens.size() ? term + 1 : term;
      } else {
        ++i;
      }
    }
    if (!result.empty()) return NormalizeType(result);
  }
  // Class fields.
  if (const ScriptClassInfo* cls = parsed.ClassAt(offset)) {
    for (const ScriptSymbol& symbol : parsed.symbols) {
      if ((symbol.kind == ScriptSymbolKind::kField ||
           symbol.kind == ScriptSymbolKind::kMethod) &&
          symbol.owner == cls->name && symbol.name == name) {
        if (symbol.kind == ScriptSymbolKind::kField) return symbol.type;
      }
    }
  }
  // Globals.
  for (const auto& [global_name, global_type] : parsed.globals) {
    if (global_name == name) return NormalizeType(global_type);
  }
  // Native globals and method/property return types.
  for (const ScriptSymbol& symbol : native_symbols) {
    if (symbol.name == name && (symbol.kind == ScriptSymbolKind::kGlobal ||
                                symbol.kind == ScriptSymbolKind::kField)) {
      return NormalizeType(symbol.type);
    }
  }
  return {};
}

std::string ResolveBaseType(const ParsedScript& parsed, size_t dot,
                            const std::vector<ScriptSymbol>& native_symbols,
                            const ScriptWorkspace* workspace) {
  const ScriptToken* last = nullptr;
  for (const ScriptToken& token : parsed.tokens) {
    if (token.end > dot) break;
    last = &token;
  }
  if (!last) return {};
  if (last->kind == ScriptToken::Kind::kIdentifier) {
    return ResolveNameType(parsed, dot, last->text, native_symbols);
  }
  if (last->text == ")") {
    // Find the matching '(' and the callee before it.
    int depth = 0;
    size_t open = parsed.tokens.size();
    for (size_t i = parsed.tokens.size(); i > 0; --i) {
      const ScriptToken& token = parsed.tokens[i - 1];
      if (token.end > dot) continue;
      if (token.text == ")") {
        ++depth;
      } else if (token.text == "(") {
        --depth;
        if (depth == 0) {
          open = i - 1;
          break;
        }
      }
    }
    if (open == parsed.tokens.size() || open == 0) return {};
    std::string callee;
    for (size_t i = open; i > 0; --i) {
      if (parsed.tokens[i - 1].kind == ScriptToken::Kind::kIdentifier) {
        callee = parsed.tokens[i - 1].text;
        break;
      }
    }
    if (callee.empty()) return {};
    for (const ScriptSymbol& symbol : parsed.symbols) {
      if (symbol.name == callee &&
          (symbol.kind == ScriptSymbolKind::kFunction ||
           symbol.kind == ScriptSymbolKind::kMethod)) {
        return NormalizeType(symbol.type);
      }
    }
    if (workspace)
      for (const ParsedScript& file : workspace->Files()) {
        if (file.res_path == parsed.res_path) continue;
        for (const ScriptSymbol& symbol : file.symbols) {
          if (symbol.name == callee &&
              (symbol.kind == ScriptSymbolKind::kFunction ||
               symbol.kind == ScriptSymbolKind::kMethod)) {
            return NormalizeType(symbol.type);
          }
        }
      }
    for (const ScriptSymbol& symbol : native_symbols) {
      if (symbol.name == callee &&
          (symbol.kind == ScriptSymbolKind::kFunction ||
           symbol.kind == ScriptSymbolKind::kMethod)) {
        return NormalizeType(symbol.type);
      }
    }
  }
  return {};
}

void AddSymbolItems(std::vector<CompletionItem>* items,
                    std::unordered_map<std::string, size_t>* seen,
                    const std::vector<ScriptSymbol>& symbols,
                    const std::function<bool(const ScriptSymbol&)>& filter,
                    int priority) {
  for (const ScriptSymbol& symbol : symbols) {
    if (!filter(symbol)) continue;
    std::string detail = symbol.signature;
    if (detail.empty()) detail = symbol.type;
    AddItem(items, seen, symbol.name, detail, priority);
  }
}

}  // namespace

std::string InferExpressionType(const ParsedScript& parsed, std::size_t dot) {
  static const std::vector<ScriptSymbol> kNoNative;
  return ResolveBaseType(parsed, dot, kNoNative, nullptr);
}

std::vector<CompletionItem> CompleteAt(
    const ScriptWorkspace& workspace, const std::string& res_path,
    const std::string& text, std::size_t offset,
    const std::vector<ScriptSymbol>& native_symbols) {
  const ParsedScript parsed = ParseScript(res_path, text);
  std::vector<CompletionItem> items;
  std::unordered_map<std::string, size_t> seen;

  const ScriptToken* last = nullptr;
  for (const ScriptToken& token : parsed.tokens) {
    if (token.end > offset) break;
    last = &token;
  }

  if (last && last->text == ".") {
    const std::string base =
        ResolveBaseType(parsed, last->begin, native_symbols, &workspace);
    auto member_filter = [&](const ScriptSymbol& symbol) {
      if (symbol.kind != ScriptSymbolKind::kMethod &&
          symbol.kind != ScriptSymbolKind::kField) {
        return false;
      }
      return base.empty() || NormalizeType(symbol.owner) == base;
    };
    AddSymbolItems(&items, &seen, parsed.symbols, member_filter, 0);
    AddSymbolItems(&items, &seen, native_symbols, member_filter, 0);
    for (const ParsedScript& file : workspace.Files()) {
      if (file.res_path == res_path) continue;
      AddSymbolItems(&items, &seen, file.symbols, member_filter, 0);
    }
    if (items.empty() || base.empty()) {
      // Unknown receiver: still offer globals so typing can continue.
      AddSymbolItems(
          &items, &seen, parsed.symbols,
          [](const ScriptSymbol& s) {
            return s.kind == ScriptSymbolKind::kFunction ||
                   s.kind == ScriptSymbolKind::kGlobal;
          },
          3);
      AddSymbolItems(
          &items, &seen, native_symbols,
          [](const ScriptSymbol& s) {
            return s.kind == ScriptSymbolKind::kFunction ||
                   s.kind == ScriptSymbolKind::kGlobal;
          },
          3);
    }
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
      if (a.priority != b.priority) return a.priority < b.priority;
      return a.label < b.label;
    });
    return items;
  }

  if (last && last->text == "::") {
    std::string qualifier;
    for (size_t i = parsed.tokens.size(); i > 0; --i) {
      const ScriptToken& token = parsed.tokens[i - 1];
      if (token.begin >= last->begin) continue;
      if (token.kind == ScriptToken::Kind::kIdentifier) {
        qualifier = token.text;
        break;
      }
    }
    auto qual_filter = [&](const ScriptSymbol& symbol) {
      if (symbol.owner != qualifier) return false;
      return symbol.kind == ScriptSymbolKind::kFunction ||
             symbol.kind == ScriptSymbolKind::kGlobal ||
             symbol.kind == ScriptSymbolKind::kEnumValue ||
             symbol.kind == ScriptSymbolKind::kClass ||
             symbol.kind == ScriptSymbolKind::kFuncdef;
    };
    AddSymbolItems(&items, &seen, parsed.symbols, qual_filter, 0);
    AddSymbolItems(&items, &seen, native_symbols, qual_filter, 0);
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
      if (a.priority != b.priority) return a.priority < b.priority;
      return a.label < b.label;
    });
    return items;
  }

  const std::string prefix = LastWord(parsed, offset);
  (void)prefix;

  // Locals and parameters.
  if (const ScriptFunctionInfo* fn = parsed.FunctionAt(offset)) {
    for (const auto& [param, type] : fn->params) {
      AddItem(&items, &seen, param, type, 0);
    }
    size_t i = 0;
    while (i < parsed.tokens.size() && parsed.tokens[i].begin < fn->body_open)
      ++i;
    while (i < parsed.tokens.size() && parsed.tokens[i].begin < offset) {
      std::string name;
      std::string type;
      size_t term = i;
      if (TryLocalDeclaration(parsed, i, parsed.tokens.size(), &name, &type,
                              &term)) {
        AddItem(&items, &seen, name, type, 0);
        i = term < parsed.tokens.size() ? term + 1 : term;
      } else {
        ++i;
      }
    }
  }
  // Members of the enclosing class.
  if (const ScriptClassInfo* cls = parsed.ClassAt(offset)) {
    AddSymbolItems(
        &items, &seen, parsed.symbols,
        [&](const ScriptSymbol& s) {
          return s.owner == cls->name && (s.kind == ScriptSymbolKind::kField ||
                                          s.kind == ScriptSymbolKind::kMethod);
        },
        1);
  }
  // Project symbols.
  AddSymbolItems(
      &items, &seen, parsed.symbols,
      [](const ScriptSymbol& s) {
        return s.kind == ScriptSymbolKind::kClass ||
               s.kind == ScriptSymbolKind::kFunction ||
               s.kind == ScriptSymbolKind::kGlobal ||
               s.kind == ScriptSymbolKind::kEnum ||
               s.kind == ScriptSymbolKind::kNamespace ||
               s.kind == ScriptSymbolKind::kFuncdef ||
               s.kind == ScriptSymbolKind::kTypedef;
      },
      2);
  for (const ParsedScript& file : workspace.Files()) {
    if (file.res_path == res_path) continue;
    AddSymbolItems(
        &items, &seen, file.symbols,
        [](const ScriptSymbol& s) {
          return s.kind == ScriptSymbolKind::kClass ||
                 s.kind == ScriptSymbolKind::kFunction ||
                 s.kind == ScriptSymbolKind::kGlobal ||
                 s.kind == ScriptSymbolKind::kEnum ||
                 s.kind == ScriptSymbolKind::kNamespace ||
                 s.kind == ScriptSymbolKind::kFuncdef ||
                 s.kind == ScriptSymbolKind::kTypedef;
        },
        2);
  }
  // Native API.
  AddSymbolItems(
      &items, &seen, native_symbols,
      [](const ScriptSymbol& s) {
        return s.kind == ScriptSymbolKind::kClass ||
               s.kind == ScriptSymbolKind::kFunction ||
               s.kind == ScriptSymbolKind::kGlobal ||
               s.kind == ScriptSymbolKind::kEnum ||
               s.kind == ScriptSymbolKind::kFuncdef ||
               s.kind == ScriptSymbolKind::kTypedef;
      },
      3);
  // Keywords.
  {
    const char* list = KeywordList();
    std::string word;
    for (const char* p = list; *p; ++p) {
      if (*p == ' ') {
        AddItem(&items, &seen, word, "keyword", 5);
        word.clear();
      } else {
        word.push_back(*p);
      }
    }
    if (!word.empty()) AddItem(&items, &seen, word, "keyword", 5);
  }
  std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.label < b.label;
  });
  return items;
}

std::vector<ScriptLocation> FindDefinitionsAt(
    const ScriptWorkspace& workspace, const std::string& res_path,
    const std::string& text, std::size_t offset,
    const std::vector<ScriptSymbol>& native_symbols) {
  const ParsedScript parsed = ParseScript(res_path, text);
  const std::string word = LastWord(parsed, offset);
  std::vector<ScriptLocation> locations;
  if (word.empty()) return locations;
  auto add = [&](const ScriptSymbol& symbol) {
    if (symbol.file.empty()) return;
    for (const ScriptLocation& existing : locations) {
      if (existing.file == symbol.file && existing.line == symbol.line) return;
    }
    locations.push_back({symbol.file, symbol.line, symbol.column});
  };
  for (const ScriptSymbol& symbol : parsed.symbols) {
    if (symbol.name == word) add(symbol);
  }
  for (const ParsedScript& file : workspace.Files()) {
    if (file.res_path == res_path) continue;
    for (const ScriptSymbol& symbol : file.symbols) {
      if (symbol.name == word) add(symbol);
    }
  }
  (void)native_symbols;
  return locations;
}

}  // namespace Mercury::EditorUI
