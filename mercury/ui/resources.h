/*
 * resources.h
 *
 * Loads and caches what UI documents refer to: UXML files, USS sheets,
 * fonts and images. Paths may be:
 *   res://ui/hud.uxml      a file in the game project (needs a resolver)
 *   mercury://fonts/x.ttf  a file shipped with the engine (mercury/ui/resources)
 *   /abs/path.png          a plain file
 */
#ifndef MERCURY_UI_RESOURCES_H
#define MERCURY_UI_RESOURCES_H

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ui/font.h"
#include "ui/style_sheet.h"
#include "ui/ui_texture.h"

namespace Mercury::UI {

class VisualTreeAsset;

class UIResources {
 public:
  using Resolver = std::function<std::filesystem::path(std::string_view)>;

  UIResources();
  ~UIResources();

  // Maps res:// paths to files; without one they are relative to the
  // working directory.
  void SetResolver(Resolver resolver) { resolver_ = std::move(resolver); }
  std::filesystem::path Resolve(std::string_view path) const;

  std::shared_ptr<StyleSheet> LoadStyleSheet(std::string_view path);
  std::shared_ptr<VisualTreeAsset> LoadUxml(std::string_view path);
  // "" is the default font.
  std::shared_ptr<Font> LoadFont(std::string_view path);
  std::shared_ptr<Font> DefaultFont();
  std::shared_ptr<UITexture> LoadTexture(std::string_view path);
  // Unity's default runtime theme, recreated in mercury://default_theme.uss.
  std::shared_ptr<StyleSheet> DefaultTheme();

  // Serve these in-memory copies instead of the files (the UI Builder's
  // unsaved edits).
  void Override(const std::string& path, std::shared_ptr<StyleSheet> sheet);
  void Override(const std::string& path, std::shared_ptr<VisualTreeAsset> tree);
  void ClearOverrides();
  // Forgets every cached file so the next load reads it again.
  void Reload();

  // The sheet after its @imports, in cascade order.
  void CollectSheets(const std::shared_ptr<StyleSheet>& sheet,
                     std::vector<const StyleSheet*>& out);

  // Increases whenever cached content changes, so panels restyle.
  uint64_t version() const { return version_; }

  // Shown when a file can't be read or parsed; defaults to PDebug::warn.
  std::function<void(const std::string&)> on_error;

  // The folder mercury:// paths point into.
  static std::filesystem::path EngineResourceDir();

 private:
  void Error(const std::string& message);

  Resolver resolver_;
  std::map<std::string, std::shared_ptr<StyleSheet>, std::less<>> sheets_;
  std::map<std::string, std::shared_ptr<VisualTreeAsset>, std::less<>> trees_;
  std::map<std::string, std::shared_ptr<Font>, std::less<>> fonts_;
  std::map<std::string, std::shared_ptr<UITexture>, std::less<>> textures_;
  std::map<std::string, std::shared_ptr<StyleSheet>, std::less<>> sheet_overrides_;
  std::map<std::string, std::shared_ptr<VisualTreeAsset>, std::less<>>
      tree_overrides_;
  std::shared_ptr<StyleSheet> default_theme_;
  uint64_t version_ = 1;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_RESOURCES_H
