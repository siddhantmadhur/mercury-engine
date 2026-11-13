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

#include "editor_scene.h"

namespace Mercury::EditorUI {

// Single text input dialog (e.g. New Map, New Folder, New Branch).
std::optional<std::string> PromptTextDialog(
    wxWindow* parent, const wxString& title, const wxString& label,
    const wxString& initial_value = wxEmptyString,
    const wxString& confirm_label = "Create");

// Script class name dialog with identifier check and snake_case path preview.
std::optional<std::string> ShowNewScriptDialog(wxWindow* parent);

// Build Game (Release) modal dialog.
bool ShowReleaseBuildDialog(wxWindow* parent, EditorScene& scene);

// Preferences: external script editor & Lore VCS settings.
bool ShowPreferencesDialog(wxWindow* parent, EditorScene& scene);

// Confirm Save / Don't Save / Cancel before closing project or quitting.
enum class UnsavedChoice { kSave, kDontSave, kCancel };
UnsavedChoice ShowUnsavedChangesDialog(wxWindow* parent,
                                       const wxString& message);

// Delete confirmation dialog for files/folders.
bool ShowDeleteConfirmDialog(wxWindow* parent,
                             const std::filesystem::path& target);

// Searchable grouped component picker. Returns chosen component name, or
// "__NEW_SCRIPT__" if user clicks "New Script...", or std::nullopt if cancelled.
std::optional<std::string> ShowAddComponentDialog(wxWindow* parent,
                                                  const MapNode& node);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_DIALOGS_H
