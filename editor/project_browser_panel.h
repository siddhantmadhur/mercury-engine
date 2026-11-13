/*
 * project_browser_panel.h
 *
 * Dockable Project Browser with folder tree, color-coded file view, search,
 * + Create menu, Lore lock badges, and bidirectional OS/internal drag-and-drop.
 */
#ifndef MERCURY_EDITOR_PROJECT_BROWSER_PANEL_H
#define MERCURY_EDITOR_PROJECT_BROWSER_PANEL_H

#include <wx/listctrl.h>
#include <wx/splitter.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "editor_scene.h"

namespace Mercury::EditorUI {

class ProjectBrowserPanel : public wxPanel {
 public:
  ProjectBrowserPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController();

 private:
  void RefreshAll(bool force_tree = false);
  void RebuildFolderTree();
  void PopulateFolderChildren(const wxTreeItemId& parent_item,
                              const std::filesystem::path& dir);
  void RebuildFileList();

  void ShowCreateMenu(const std::filesystem::path& target_dir, wxWindow* anchor,
                      const wxPoint& pos);
  void ShowFolderContextMenu(const std::filesystem::path& dir,
                             const wxPoint& screen_pos);
  void ShowFileContextMenu(const std::filesystem::path& file,
                           const wxPoint& screen_pos);
  bool CopyExternalFilesInto(const std::filesystem::path& dest_dir,
                             const wxArrayString& files);
  void HandleCreateScript();
  void HandleCreateMap(const std::filesystem::path& dir);
  void HandleCreateFolder(const std::filesystem::path& dir);
  void HandleDeletePath(const std::filesystem::path& target);

  EditorScene& scene_;
  wxStaticText* path_label_ = nullptr;
  wxTextCtrl* search_ = nullptr;
  wxSplitterWindow* splitter_ = nullptr;
  wxTreeCtrl* dir_tree_ = nullptr;
  wxListCtrl* file_list_ = nullptr;

  std::filesystem::path current_dir_;
  std::filesystem::path last_project_root_;
  std::vector<std::filesystem::path> visible_files_;
  std::unordered_map<std::string, wxTreeItemId> path_to_tree_;

  uint64_t last_map_rev_ = 0;
  uint64_t last_sel_rev_ = 0;
  uint64_t last_lore_rev_ = 0;
  uint64_t last_scan_ms_ = 0;
  std::string last_signature_;
  bool syncing_tree_ = false;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_PROJECT_BROWSER_PANEL_H
