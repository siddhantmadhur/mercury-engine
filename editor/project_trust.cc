// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "project_trust.h"

#include <system_error>

#include "console.h"
#include "editor_settings.h"

namespace Mercury::EditorUI {

namespace {

namespace fs = std::filesystem;

bool RemoveEntry(const fs::path& path) {
  std::error_code ec;
  const fs::file_status status = fs::symlink_status(path, ec);
  if (ec || status.type() == fs::file_type::not_found) return false;
  if (fs::is_symlink(status)) {
    // Remove the link itself, never what it points at.
    fs::remove(path, ec);
  } else if (fs::is_directory(status)) {
    // remove_all does not follow symlinks inside the tree either.
    fs::remove_all(path, ec);
  } else {
    fs::remove(path, ec);
  }
  return !ec;
}

}  // namespace

void SetProjectTrust(EditorSettings& settings, const fs::path& root,
                     bool trusted, Console& console) {
  const bool was_trusted = settings.IsProjectTrusted(root);
  settings.SetProjectTrusted(root, trusted);
  settings.Save();
  if (!trusted || was_trusted) return;

  std::error_code ec;
  const fs::path cache = root / ".mercury";
  const fs::file_status cache_status = fs::symlink_status(cache, ec);
  if (fs::is_symlink(cache_status)) {
    console.Warn(
        "Not clearing .mercury: it is a symlink, so its contents are not part "
        "of this project.");
    return;
  }
  bool removed = false;
  removed |= RemoveEntry(cache / "build");
  removed |= RemoveEntry(cache / "systems");
  if (removed) {
    console.Info("Cleared pre-existing build output in " + cache.string() +
                 " before the first trusted build.");
  }
}

}  // namespace Mercury::EditorUI
