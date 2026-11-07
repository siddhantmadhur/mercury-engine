/*
 * source_map.cc
 */
#include "scripting/js/source_map.h"

#include <algorithm>
#include <cctype>

namespace Mercury {

namespace {

// Reads a JSON string starting at the opening quote; moves `i` past the closing one.
bool ReadString(std::string_view s, size_t& i, std::string* out) {
  if (i >= s.size() || s[i] != '"') return false;
  i++;
  out->clear();
  while (i < s.size() && s[i] != '"') {
    char c = s[i++];
    if (c != '\\') {
      out->push_back(c);
      continue;
    }
    if (i >= s.size()) return false;
    char e = s[i++];
    switch (e) {
      case 'n': out->push_back('\n'); break;
      case 't': out->push_back('\t'); break;
      case 'r': out->push_back('\r'); break;
      case 'b': out->push_back('\b'); break;
      case 'f': out->push_back('\f'); break;
      case 'u': {
        if (i + 4 > s.size()) return false;
        unsigned code = 0;
        for (int k = 0; k < 4; k++) {
          char h = s[i++];
          code = code * 16 + (std::isdigit(static_cast<unsigned char>(h)) ? h - '0'
                                                                           : (std::tolower(h) - 'a' + 10));
        }
        if (code < 0x80) {
          out->push_back(static_cast<char>(code));
        } else if (code < 0x800) {
          out->push_back(static_cast<char>(0xC0 | (code >> 6)));
          out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
          out->push_back(static_cast<char>(0xE0 | (code >> 12)));
          out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
          out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
        break;
      }
      default: out->push_back(e); break;  // \" \\ \/
    }
  }
  if (i >= s.size()) return false;
  i++;
  return true;
}

void SkipSpace(std::string_view s, size_t& i) {
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
}

// Skips any JSON value.
bool SkipValue(std::string_view s, size_t& i) {
  SkipSpace(s, i);
  if (i >= s.size()) return false;
  if (s[i] == '"') {
    std::string tmp;
    return ReadString(s, i, &tmp);
  }
  if (s[i] == '{' || s[i] == '[') {
    int depth = 0;
    while (i < s.size()) {
      if (s[i] == '"') {
        std::string tmp;
        if (!ReadString(s, i, &tmp)) return false;
        continue;
      }
      if (s[i] == '{' || s[i] == '[') depth++;
      if (s[i] == '}' || s[i] == ']') depth--;
      i++;
      if (depth == 0) return true;
    }
    return false;
  }
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') i++;
  return true;
}

int Base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Decodes one base64 VLQ number; returns false on bad input.
bool ReadVlq(std::string_view s, size_t& i, int* out) {
  int result = 0;
  int shift = 0;
  while (true) {
    if (i >= s.size()) return false;
    int digit = Base64Value(s[i++]);
    if (digit < 0) return false;
    result += (digit & 31) << shift;
    if (!(digit & 32)) break;
    shift += 5;
    if (shift > 30) return false;
  }
  *out = (result & 1) ? -(result >> 1) : (result >> 1);
  return true;
}

}  // namespace

bool SourceMap::Parse(std::string_view json, std::string* error) {
  sources_.clear();
  lines_.clear();
  auto fail = [&](const char* message) {
    if (error) *error = message;
    return false;
  };

  size_t i = 0;
  SkipSpace(json, i);
  if (i >= json.size() || json[i] != '{') return fail("source map is not a JSON object");
  i++;
  std::string mappings;
  int version = 0;
  while (true) {
    SkipSpace(json, i);
    if (i >= json.size()) return fail("unterminated source map");
    if (json[i] == '}') break;
    if (json[i] == ',') {
      i++;
      continue;
    }
    std::string key;
    if (!ReadString(json, i, &key)) return fail("bad key in source map");
    SkipSpace(json, i);
    if (i >= json.size() || json[i] != ':') return fail("bad source map");
    i++;
    SkipSpace(json, i);
    if (key == "version") {
      version = std::atoi(std::string(json.substr(i, 8)).c_str());
      SkipValue(json, i);
    } else if (key == "mappings") {
      if (!ReadString(json, i, &mappings)) return fail("bad mappings");
    } else if (key == "sources") {
      if (i >= json.size() || json[i] != '[') return fail("bad sources");
      i++;
      while (true) {
        SkipSpace(json, i);
        if (i >= json.size()) return fail("bad sources");
        if (json[i] == ']') {
          i++;
          break;
        }
        if (json[i] == ',') {
          i++;
          continue;
        }
        std::string name;
        if (!ReadString(json, i, &name)) return fail("bad sources");
        sources_.push_back(std::move(name));
      }
    } else if (!SkipValue(json, i)) {
      return fail("bad source map");
    }
  }
  if (version != 3) return fail("unsupported source map version");

  // mappings: segments split by ',', generated lines split by ';'.
  lines_.emplace_back();
  int source = 0, line = 0, column = 0;
  int generated_column = 0;
  size_t p = 0;
  while (p < mappings.size()) {
    char c = mappings[p];
    if (c == ';') {
      lines_.emplace_back();
      generated_column = 0;
      p++;
      continue;
    }
    if (c == ',') {
      p++;
      continue;
    }
    int delta = 0;
    if (!ReadVlq(mappings, p, &delta)) return fail("bad mappings");
    generated_column += delta;
    // A segment has 1, 4 or 5 fields; only 4 and 5 point at a source.
    int fields[4];
    int count = 0;
    while (count < 4 && p < mappings.size() && mappings[p] != ',' && mappings[p] != ';') {
      if (!ReadVlq(mappings, p, &fields[count])) return fail("bad mappings");
      count++;
    }
    while (p < mappings.size() && mappings[p] != ',' && mappings[p] != ';') {
      int ignored;
      if (!ReadVlq(mappings, p, &ignored)) return fail("bad mappings");
    }
    if (count >= 3) {
      source += fields[0];
      line += fields[1];
      column += fields[2];
      lines_.back().push_back({generated_column, source, line, column});
    }
  }
  for (auto& segments : lines_) {
    std::stable_sort(segments.begin(), segments.end(), [](const Segment& a, const Segment& b) {
      return a.generated_column < b.generated_column;
    });
  }
  return true;
}

std::optional<SourcePosition> SourceMap::Lookup(int line, int column) const {
  if (line < 1 || column < 1 || static_cast<size_t>(line) > lines_.size()) return std::nullopt;
  const auto& segments = lines_[line - 1];
  const int target = column - 1;
  // The last segment that starts at or before the column.
  auto it = std::upper_bound(segments.begin(), segments.end(), target,
                             [](int value, const Segment& s) { return value < s.generated_column; });
  if (it == segments.begin()) return std::nullopt;
  --it;
  if (it->source < 0 || static_cast<size_t>(it->source) >= sources_.size()) return std::nullopt;
  return SourcePosition{sources_[it->source], it->line + 1, it->column + 1};
}

}  // namespace Mercury
