#include "resources.h"

#include <fstream>
#include <sstream>

#include "debugger/debugger.h"
#include "ui/uxml.h"

#ifndef MERCURY_UI_RESOURCE_PATH
#define MERCURY_UI_RESOURCE_PATH "mercury/ui/resources"
#endif

namespace Mercury::UI {

namespace {

constexpr std::string_view kEnginePrefix = "mercury://";

bool ReadText(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

}  // namespace

UIResources::UIResources() = default;
UIResources::~UIResources() = default;

std::filesystem::path UIResources::EngineResourceDir() {
  return std::filesystem::path(MERCURY_UI_RESOURCE_PATH);
}

void UIResources::Error(const std::string& message) {
  if (on_error) {
    on_error(message);
  } else {
    PDebug::warn("UI: {}", message);
  }
}

std::filesystem::path UIResources::Resolve(std::string_view path) const {
  if (path.starts_with(kEnginePrefix)) {
    return EngineResourceDir() / std::string(path.substr(kEnginePrefix.size()));
  }
  if (path.starts_with("res://")) {
    if (resolver_) return resolver_(path);
    return std::filesystem::path(std::string(path.substr(6)));
  }
  return std::filesystem::path(std::string(path));
}

std::shared_ptr<StyleSheet> UIResources::LoadStyleSheet(std::string_view path) {
  if (path.empty()) return nullptr;
  if (auto it = sheet_overrides_.find(path); it != sheet_overrides_.end()) {
    return it->second;
  }
  if (auto it = sheets_.find(path); it != sheets_.end()) return it->second;
  std::string text;
  std::shared_ptr<StyleSheet> sheet;
  if (!ReadText(Resolve(path), text)) {
    Error("could not read style sheet " + std::string(path));
    sheet = std::make_shared<StyleSheet>();
    sheet->path = std::string(path);
  } else {
    std::vector<StyleParseError> errors;
    sheet = StyleSheet::Parse(text, std::string(path), &errors);
    for (const auto& e : errors) {
      Error(std::format("{}:{}: {}", path, e.line, e.message));
    }
  }
  sheets_.emplace(std::string(path), sheet);
  return sheet;
}

std::shared_ptr<VisualTreeAsset> UIResources::LoadUxml(std::string_view path) {
  if (path.empty()) return nullptr;
  if (auto it = tree_overrides_.find(path); it != tree_overrides_.end()) {
    return it->second;
  }
  if (auto it = trees_.find(path); it != trees_.end()) return it->second;
  std::string text;
  std::shared_ptr<VisualTreeAsset> tree;
  if (!ReadText(Resolve(path), text)) {
    Error("could not read UXML " + std::string(path));
  } else {
    std::vector<UxmlError> errors;
    tree = VisualTreeAsset::Parse(text, std::string(path), &errors);
    for (const auto& e : errors) {
      Error(std::format("{}:{}: {}", path, e.line, e.message));
    }
  }
  trees_.emplace(std::string(path), tree);
  return tree;
}

std::shared_ptr<Font> UIResources::DefaultFont() {
  return LoadFont("mercury://fonts/Roboto-Medium.ttf");
}

std::shared_ptr<Font> UIResources::LoadFont(std::string_view path) {
  if (path.empty()) return DefaultFont();
  if (auto it = fonts_.find(path); it != fonts_.end()) {
    if (it->second) return it->second;
    return path == "mercury://fonts/Roboto-Medium.ttf" ? nullptr : DefaultFont();
  }
  auto font = Font::LoadFile(Resolve(path).string());
  fonts_.emplace(std::string(path), font);
  if (!font) {
    Error("could not load font " + std::string(path));
    if (path != "mercury://fonts/Roboto-Medium.ttf") return DefaultFont();
  }
  return font;
}

std::shared_ptr<UITexture> UIResources::LoadTexture(std::string_view path) {
  if (path.empty()) return nullptr;
  if (auto it = textures_.find(path); it != textures_.end()) return it->second;
  auto texture = UITextureRegistry::Instance().LoadFile(Resolve(path).string());
  textures_.emplace(std::string(path), texture);
  return texture;
}

std::shared_ptr<StyleSheet> UIResources::DefaultTheme() {
  if (!default_theme_) default_theme_ = LoadStyleSheet("mercury://default_theme.uss");
  return default_theme_;
}

void UIResources::Override(const std::string& path, std::shared_ptr<StyleSheet> sheet) {
  sheet_overrides_[path] = std::move(sheet);
  version_++;
}

void UIResources::Override(const std::string& path, std::shared_ptr<VisualTreeAsset> tree) {
  tree_overrides_[path] = std::move(tree);
  version_++;
}

void UIResources::ClearOverrides() {
  sheet_overrides_.clear();
  tree_overrides_.clear();
  version_++;
}

void UIResources::Reload() {
  sheets_.clear();
  trees_.clear();
  textures_.clear();
  default_theme_.reset();
  version_++;
}

void UIResources::CollectSheets(const std::shared_ptr<StyleSheet>& sheet,
                                std::vector<const StyleSheet*>& out) {
  if (!sheet) return;
  // Guards against import cycles.
  if (std::find(out.begin(), out.end(), sheet.get()) != out.end()) return;
  for (const auto& import : sheet->imports) {
    CollectSheets(LoadStyleSheet(import), out);
  }
  out.push_back(sheet.get());
}

}  // namespace Mercury::UI
