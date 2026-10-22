#include "editor_settings.h"

#include <algorithm>
#include <cstdlib>

#include "serialization/mtext.h"

namespace Mercury {

namespace fs = std::filesystem;

namespace {
constexpr size_t kMaxRecentProjects = 10;
}

fs::path EditorSettings::ConfigDir() {
#if defined(_WIN32)
  if (const char* appdata = std::getenv("APPDATA")) {
    return fs::path(appdata) / "Mercury";
  }
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
    return fs::path(xdg) / "mercury";
  }
  if (const char* home = std::getenv("HOME")) {
    return fs::path(home) / ".config" / "mercury";
  }
#endif
  return fs::current_path() / ".mercury-editor";
}

fs::path EditorSettings::HomeDir() {
#if defined(_WIN32)
  const char* home = std::getenv("USERPROFILE");
#else
  const char* home = std::getenv("HOME");
#endif
  return home ? fs::path(home) : fs::current_path();
}

void EditorSettings::Load() {
  mtext::Document document;
  if (!mtext::LoadFile(ConfigDir() / "editor.cfg", document)) return;
  if (auto editor = document.Find("editor")) {
    external_editor = editor->properties.GetString("external_editor");
  }
  if (auto windows = document.Find("windows")) {
    show_ui_builder = windows->properties.GetBool("ui_builder", false);
  }
  if (auto recent = document.Find("recent")) {
    if (auto list = recent->properties.Get("projects")) {
      if (auto array = list->Get<mtext::Array>()) {
        for (const auto& item : *array) {
          if (auto path = item.ToString()) recent_projects.push_back(*path);
        }
      }
    }
  }
}

void EditorSettings::Save() const {
  mtext::Document document;
  document.Add("editor").properties.Set("external_editor", external_editor);
  mtext::Array projects;
  for (const auto& path : recent_projects) projects.emplace_back(path);
  document.Add("recent").properties.Set("projects", projects);
  document.Add("windows").properties.Set("ui_builder", show_ui_builder);
  mtext::SaveFile(ConfigDir() / "editor.cfg", document);
}

void EditorSettings::AddRecentProject(const fs::path& mproj) {
  const std::string path = mproj.string();
  std::erase(recent_projects, path);
  recent_projects.insert(recent_projects.begin(), path);
  if (recent_projects.size() > kMaxRecentProjects) {
    recent_projects.resize(kMaxRecentProjects);
  }
}

}  // namespace Mercury
