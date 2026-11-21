// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * dialogs.h
 *
 * Native dark-themed modal dialogs for Mercury Editor (Create Script, New Map,
 * New Folder, Release Build, Settings, Unsaved Changes, Add Component, Delete).
 */
#ifndef MERCURY_EDITOR_DIALOGS_H
#define MERCURY_EDITOR_DIALOGS_H

#include <wx/wx.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "editor_scene.h"
#include "templates.h"

namespace Mercury::EditorUI {

// Single text input dialog (e.g. New Map, New Folder, New Branch).
std::optional<std::string> PromptTextDialog(
    wxWindow* parent, const wxString& title, const wxString& label,
    const wxString& initial_value = wxEmptyString,
    const wxString& confirm_label = "Create");

// Script class name dialog with identifier check and snake_case path preview.
struct NewScriptRequest {
  Templates::ScriptKind kind = Templates::ScriptKind::kAngelScript;
  std::string class_name;
  // Initialized from EditorSettings::open_script_after_create; the caller
  // persists both this and the chosen kind when the user accepts.
  bool open_after_create = true;
};

/**
 * @brief New Script dialog: choose a kind, class name and files to create
 * @param parent Parent window for the modal dialog.
 * @param scene Project whose settings and root the dialog uses.
 * @param allowed_kinds Restricts the Kind choices (empty means all four). The
 *        last used kind is preselected when it is allowed.
 */
std::optional<NewScriptRequest> ShowNewScriptDialog(
    wxWindow* parent, EditorScene& scene,
    const std::vector<Templates::ScriptKind>& allowed_kinds = {});

// Build Game (Release) modal dialog.
bool ShowReleaseBuildDialog(wxWindow* parent, EditorScene& scene);

/**
 * @brief Asks (once) before building/running a project's C++ code
 * @return false when the user cancelled or the project is marked untrusted
 */
bool EnsureNativeProjectTrusted(wxWindow* parent, EditorScene& scene);

// Preferences: General (external script editor & Lore VCS) and MCP (local
// server for AI tools) tabs.
bool ShowPreferencesDialog(wxWindow* parent, EditorScene& scene);

// Trust prompt for a project whose system/ folder contains native editor
// code. Returns true (trust), false (do not trust) or std::nullopt (cancel /
// decide later; the caller should not save a decision).
std::optional<bool> ShowTrustSystemsDialog(
    wxWindow* parent, const std::filesystem::path& root,
    const std::string& systems_target, const std::filesystem::path& source_dir,
    bool currently_trusted, bool first_time);

// Confirm Save / Don't Save / Cancel before closing project or quitting.
enum class UnsavedChoice { kSave, kDontSave, kCancel };
UnsavedChoice ShowUnsavedChangesDialog(wxWindow* parent,
                                       const wxString& message);

// Delete confirmation dialog for files/folders.
bool ShowDeleteConfirmDialog(wxWindow* parent,
                             const std::filesystem::path& target);

// Searchable grouped component picker. Returns chosen component name, or
// "__NEW_SCRIPT__" if user clicks "New Script...", or std::nullopt if
// cancelled.
std::optional<std::string> ShowAddComponentDialog(wxWindow* parent,
                                                  const MapNode& node);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_DIALOGS_H
