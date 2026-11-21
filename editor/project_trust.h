// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * project_trust.h
 *
 * The one place that records a workspace trust decision. Granting trust for
 * the first time also clears any build output that shipped with the project:
 * a prebuilt .mercury/build (CMakeCache, build.ninja) must never be executed
 * as if this editor produced it.
 */
#ifndef MERCURY_EDITOR_PROJECT_TRUST_H
#define MERCURY_EDITOR_PROJECT_TRUST_H

#include <filesystem>

namespace Mercury {

class Console;
struct EditorSettings;

namespace EditorUI {

/**
 * @brief Records a trust decision and purges stale build output on first trust
 *
 * When `trusted` is newly granted, the project's .mercury/build and
 * .mercury/systems folders are removed (symlinks are removed as links, never
 * followed) and a line is logged. Repeating the decision is cheap.
 */
void SetProjectTrust(EditorSettings& settings,
                     const std::filesystem::path& root, bool trusted,
                     Console& console);

}  // namespace EditorUI
}  // namespace Mercury

#endif  // MERCURY_EDITOR_PROJECT_TRUST_H
