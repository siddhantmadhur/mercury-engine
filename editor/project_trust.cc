// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "project_trust.h"

#include <system_error>

#include "console.h"
#include "editor_settings.h"
#include "os/filesystem.h"

namespace Mercury::EditorUI {

namespace {

namespace fs = std::filesystem;

enum class WipeResult { kNothing, kRemoved, kFailed };

WipeResult RemoveEntry(const fs::path& path, std::string* error) {
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(path, ec);
  if (ec) {
    if (error)
      *error = "could not inspect " + path.string() + ": " + ec.message();
    return WipeResult::kFailed;
  }
  if (status.type() == fs::file_type::not_found) return WipeResult::kNothing;
  if (fs::is_symlink(status)) {
    // Remove the link itself, never what it points at.
    fs::remove(path, ec);
  } else if (fs::is_directory(status)) {
    // remove_all does not follow symlinks inside the tree either.
    fs::remove_all(path, ec);
  } else {
    fs::remove(path, ec);
  }
  if (ec) {
    if (error)
      *error = "could not remove " + path.string() + ": " + ec.message();
    return WipeResult::kFailed;
  }
  return WipeResult::kRemoved;
}

}  // namespace

void SetProjectTrust(EditorSettings& settings, const fs::path& root,
                     bool trusted, Console& console) {
  const bool was_trusted = settings.IsProjectTrusted(root);
  if (!trusted || was_trusted) {
    settings.SetProjectTrusted(root, trusted);
    settings.Save();
    return;
  }

  // First grant: the cache must be a real directory, and any build output
  // shipped with the project is removed before it could be configured or run.
  std::string error;
  const fs::path cache = os::EnsureProjectCacheDir(root, &error);
  if (cache.empty()) {
    console.Error("Cannot trust this project: " + error);
    console.Error(
        "The trust decision was not saved; fix or remove .mercury and try "
        "again.");
    return;
  }

  bool removed = false;
  for (const char* name : {"build", "systems"}) {
    switch (RemoveEntry(cache / name, &error)) {
      case WipeResult::kNothing:
        break;
      case WipeResult::kRemoved:
        removed = true;
        break;
      case WipeResult::kFailed:
        console.Error("Cannot trust this project: " + error);
        console.Error(
            "The trust decision was not saved; remove that folder and try "
            "again.");
        return;
    }
  }
  if (removed) {
    console.Info("Cleared pre-existing build output in " + cache.string() +
                 " before the first trusted build.");
  }

  settings.SetProjectTrusted(root, true);
  settings.Save();
}

}  // namespace Mercury::EditorUI
