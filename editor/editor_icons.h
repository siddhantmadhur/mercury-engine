/*
 * editor_icons.h
 *
 * Resolution-independent SVG icon bundles tinted to the active native GTK
 * button foreground color, plus uniform toolbar button styling/padding.
 */
#ifndef MERCURY_EDITOR_EDITOR_ICONS_H
#define MERCURY_EDITOR_EDITOR_ICONS_H

#include <wx/anybutton.h>
#include <wx/bmpbndl.h>

namespace Mercury::EditorUI {

enum class EditorIcon {
  kSave,
  kUndo,
  kRedo,
  kPlay,
  kBuilding,
  kStop,
  kBuildScripts,
  kBuildGame,
  kMove,
  kRotate,
  kScale,
  kLocalSpace,
  kGlobalSpace,
  kView2D,
  kView3D,
  kFrame,
  kCreate,
  kClear,
  kSync,
  kPush,
  kRefresh,
  kOptions,
  kRevert,
  kDuplicate,
  kDelete,
  kPopOut,
  kDock,
  kClose,
  kCommit,
  kFit,
  kOneToOne,
  kOpenFolder,
  kNewFile,
  kPreferences,
};

// Returns a crisp multi-DPI wxBitmapBundle for the requested editor icon.
wxBitmapBundle GetEditorIcon(EditorIcon icon, int size = 15);

// Applies an icon on the left of a native wxButton/wxToggleButton and gives
// comfortable horizontal padding without fighting the active GTK theme.
void ApplyButtonIconAndPadding(wxAnyButton* btn, EditorIcon icon,
                               int extra_pad_x = 10, int icon_size = 15,
                               int margin_x = 6);

// Applies comfortable horizontal padding to a labeled button without an icon.
void ApplyButtonPadding(wxAnyButton* btn, int extra_pad_x = 10);

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_EDITOR_ICONS_H
