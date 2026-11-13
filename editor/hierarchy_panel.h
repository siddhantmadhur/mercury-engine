/*
 * hierarchy_panel.h
 *
 * Retained-mode Scene Hierarchy tree panel using wxTreeCtrl with search filter,
 * inline rename, drag-and-drop reparenting, OS model drop, and context menus.
 */
#ifndef MERCURY_EDITOR_HIERARCHY_PANEL_H
#define MERCURY_EDITOR_HIERARCHY_PANEL_H

#include <wx/treectrl.h>
#include <wx/wx.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "editor_scene.h"

namespace Mercury::EditorUI {

class HierarchyPanel : public wxPanel {
 public:
  HierarchyPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController();

 private:
  void RebuildTree();
  void PopulateChildren(const wxTreeItemId& parent_item, const MapNode& node,
                        const std::string& filter_lower);
  bool NodeMatchesFilter(const MapNode& node,
                         const std::string& filter_lower) const;
  void CollectExpandedUids();
  void ShowContextMenu(const wxPoint& screen_pos, MapNode* node);

  EditorScene& scene_;
  wxStaticText* map_label_ = nullptr;
  wxTextCtrl* search_ = nullptr;
  wxTreeCtrl* tree_ = nullptr;

  std::unordered_map<uint64_t, wxTreeItemId> uid_to_item_;
  std::unordered_set<uint64_t> expanded_uids_;
  uint64_t last_map_rev_ = 0;
  uint64_t last_sel_rev_ = 0;
  std::string last_map_label_;
  bool syncing_selection_ = false;
  bool filter_dirty_ = true;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_HIERARCHY_PANEL_H
