/*
 * launcher_frame.h
 *
 * Native OS Project Launcher window shown before a project is opened.
 * Supports Recent Projects, Open .mproj/folder dialog, OS file drag-and-drop,
 * and New Project creation (2D/3D).
 */
#ifndef MERCURY_EDITOR_LAUNCHER_FRAME_H
#define MERCURY_EDITOR_LAUNCHER_FRAME_H

#include <wx/listctrl.h>
#include <wx/wx.h>

#include <functional>
#include <string>

#include "editor_scene.h"

namespace Mercury::EditorUI {

class LauncherFrame : public wxFrame {
 public:
  LauncherFrame(EditorScene& scene, std::function<void()> on_project_opened);

  void RefreshRecentProjects();
  void SetError(const std::string& msg);

 private:
  void BuildLayout();
  void TryOpenPath(const std::string& path_str);

  EditorScene& scene_;
  std::function<void()> on_project_opened_;

  wxListCtrl* recent_list_ = nullptr;
  wxButton* open_recent_btn_ = nullptr;
  wxButton* remove_recent_btn_ = nullptr;
  wxTextCtrl* new_name_ctrl_ = nullptr;
  wxRadioButton* radio_3d_ = nullptr;
  wxRadioButton* radio_2d_ = nullptr;
  wxTextCtrl* new_folder_ctrl_ = nullptr;
  wxStaticText* error_lbl_ = nullptr;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_LAUNCHER_FRAME_H
