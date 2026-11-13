/*
 * console_panel.h
 *
 * Dockable Console panel displaying engine, build, game, and Lore logs.
 */
#ifndef MERCURY_EDITOR_CONSOLE_PANEL_H
#define MERCURY_EDITOR_CONSOLE_PANEL_H

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <cstddef>
#include <vector>

#include "console.h"
#include "editor_scene.h"

namespace Mercury::EditorUI {

class ConsolePanel : public wxPanel {
 public:
  ConsolePanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController();

 private:
  void RebuildList(bool force_scroll);

  EditorScene& scene_;
  wxCheckBox* show_info_ = nullptr;
  wxCheckBox* show_warnings_ = nullptr;
  wxCheckBox* show_errors_ = nullptr;
  wxCheckBox* show_build_ = nullptr;
  wxCheckBox* show_game_ = nullptr;
  wxCheckBox* show_lore_ = nullptr;
  wxTextCtrl* search_ = nullptr;
  wxListCtrl* list_ = nullptr;

  std::vector<Console::Line> visible_lines_;
  size_t last_version_ = 0;
  bool filter_dirty_ = true;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_CONSOLE_PANEL_H
