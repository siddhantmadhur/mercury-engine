#include "hierarchy_panel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "create_menu.h"
#include "dnd_payloads.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

class NodeItemData : public wxTreeItemData {
 public:
  explicit NodeItemData(uint64_t u) : uid(u) {}
  uint64_t uid;
};

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

HierarchyPanel::HierarchyPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY), scene_(scene) {
  ApplyDarkPalette(this);
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* bar = new wxPanel(this, wxID_ANY);
  bar->SetBackgroundColour(Colors::kBgHeader);
  auto* bar_sizer = new wxBoxSizer(wxHORIZONTAL);

  auto* create_btn = new wxButton(bar, wxID_ANY, "+ Create", wxDefaultPosition,
                                  wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(create_btn);
  create_btn->SetToolTip(
      "Add a node to the map; right-click a node to add a child");
  bar_sizer->Add(create_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  map_label_ = new wxStaticText(bar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxDefaultSize, wxST_ELLIPSIZE_END);
  map_label_->SetForegroundColour(Colors::kTextSecondary);
  bar_sizer->Add(map_label_, 1, wxALIGN_CENTER_VERTICAL);

  auto* wrap_bar = new wxBoxSizer(wxVERTICAL);
  wrap_bar->Add(bar_sizer, 0, wxEXPAND | wxALL, 4);
  bar->SetSizer(wrap_bar);
  root->Add(bar, 0, wxEXPAND);

  search_ = new wxTextCtrl(this, wxID_ANY);
  search_->SetHint("Search hierarchy...");
  StyleTextCtrl(search_);
  root->Add(search_, 0, wxEXPAND | wxALL, 4);

  tree_ = new wxTreeCtrl(
      this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_EDIT_LABELS |
          wxTR_SINGLE | wxBORDER_NONE);
  tree_->SetBackgroundColour(Colors::kBgCanvas);
  tree_->SetForegroundColour(Colors::kTextPrimary);
  root->Add(tree_, 1, wxEXPAND);
  SetSizer(root);

  create_btn->Bind(wxEVT_BUTTON, [this, create_btn](wxCommandEvent&) {
    wxMenu menu;
    PopulateCreateNodeMenu(&menu, scene_, nullptr);
    create_btn->PopupMenu(&menu, 0, create_btn->GetSize().y);
  });

  search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    filter_dirty_ = true;
    RebuildTree();
  });

  tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& evt) {
    if (syncing_selection_) return;
    const wxTreeItemId item = evt.GetItem();
    if (!item.IsOk()) {
      scene_.Select(nullptr);
      last_sel_rev_ = scene_.SelectionRevision();
      return;
    }
    auto* data = static_cast<NodeItemData*>(tree_->GetItemData(item));
    if (data) {
      scene_.Select(scene_.GetMap().FindByUid(data->uid));
      last_sel_rev_ = scene_.SelectionRevision();
    }
  });

  tree_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
    int flags = 0;
    const wxTreeItemId hit = tree_->HitTest(evt.GetPosition(), flags);
    if (!hit.IsOk()) {
      tree_->UnselectAll();
      scene_.Select(nullptr);
      last_sel_rev_ = scene_.SelectionRevision();
    }
    evt.Skip();
  });

  tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& evt) {
    const wxTreeItemId item = evt.GetItem();
    if (!item.IsOk()) return;
    auto* data = static_cast<NodeItemData*>(tree_->GetItemData(item));
    if (data) {
      if (MapNode* node = scene_.GetMap().FindByUid(data->uid);
          node && node->children.empty()) {
        tree_->EditLabel(item);
      }
    }
  });

  tree_->Bind(wxEVT_TREE_END_LABEL_EDIT, [this](wxTreeEvent& evt) {
    if (evt.IsEditCancelled()) return;
    auto* data = static_cast<NodeItemData*>(tree_->GetItemData(evt.GetItem()));
    if (!data) {
      evt.Veto();
      return;
    }
    MapNode* node = scene_.GetMap().FindByUid(data->uid);
    const std::string next = evt.GetLabel().ToStdString();
    if (!node || next.empty() || next == node->name) {
      evt.Veto();
      return;
    }
    CallAfter([this, uid = data->uid, next]() {
      if (MapNode* target = scene_.GetMap().FindByUid(uid)) {
        scene_.RenameNode(target, next);
      }
    });
  });

  tree_->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& evt) {
    auto* data = static_cast<NodeItemData*>(tree_->GetItemData(evt.GetItem()));
    if (!data) return;
    MapNode* node = scene_.GetMap().FindByUid(data->uid);
    if (!node || node->IsRoot()) return;
    std::unique_ptr<wxTextDataObject> payload(MakeNodeDragObject(node->uid));
    wxDropSource source(*payload, tree_);
    source.DoDragDrop(wxDrag_AllowMove);
  });

  tree_->Bind(wxEVT_TREE_ITEM_MENU, [this](wxTreeEvent& evt) {
    MapNode* node = nullptr;
    if (evt.GetItem().IsOk()) {
      auto* data = static_cast<NodeItemData*>(tree_->GetItemData(evt.GetItem()));
      if (data) node = scene_.GetMap().FindByUid(data->uid);
    }
    if (node) scene_.Select(node);
    ShowContextMenu(tree_->ClientToScreen(evt.GetPoint()), node);
  });

  tree_->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& evt) {
    int flags = 0;
    const wxTreeItemId hit = tree_->HitTest(evt.GetPosition(), flags);
    if (!hit.IsOk()) {
      ShowContextMenu(tree_->ClientToScreen(evt.GetPosition()), nullptr);
    } else {
      evt.Skip();
    }
  });

  tree_->SetDropTarget(new CompositeDropTarget(
      [this](wxCoord, wxCoord, const wxArrayString& files) {
        auto project = scene_.GetProject();
        if (!project) return false;
        for (const wxString& f : files) {
          const fs::path p = f.ToStdString();
          std::string ext = ToLower(p.extension().string());
          std::string res = project->ToResPath(p);
          if (ext == ".obj" && !res.empty()) {
            scene_.ImportModel(res);
            return true;
          }
        }
        return false;
      },
      [this](wxCoord x, wxCoord y, const wxString& text) {
        if (auto uid = ExtractNodeUidPayload(text)) {
          MapNode* dragged = scene_.GetMap().FindByUid(*uid);
          if (!dragged) return false;
          int flags = 0;
          const wxTreeItemId hit = tree_->HitTest(wxPoint(x, y), flags);
          MapNode* target_parent = scene_.GetMap().GetRoot();
          if (hit.IsOk()) {
            if (auto* data = static_cast<NodeItemData*>(tree_->GetItemData(hit))) {
              if (MapNode* target = scene_.GetMap().FindByUid(data->uid)) {
                target_parent = target;
              }
            }
          }
          if (dragged == target_parent) return false;
          return scene_.ReparentNode(dragged, target_parent);
        }
        if (auto res_path = ExtractResPathPayload(text)) {
          std::string ext = ToLower(fs::path(*res_path).extension().string());
          if (ext == ".obj") {
            scene_.ImportModel(*res_path);
            return true;
          }
        }
        return false;
      }));

  RebuildTree();
}

void HierarchyPanel::SyncFromController() {
  map_label_->SetLabel(wxString::FromUTF8(scene_.MapDisplayName()));
  if (scene_.MapRevision() != last_map_rev_ || filter_dirty_) {
    RebuildTree();
  } else if (scene_.SelectionRevision() != last_sel_rev_) {
    syncing_selection_ = true;
    const uint64_t sel_uid = scene_.GetSelectedUid();
    if (sel_uid != 0) {
      auto it = uid_to_item_.find(sel_uid);
      if (it != uid_to_item_.end() && it->second.IsOk()) {
        tree_->SelectItem(it->second);
        tree_->EnsureVisible(it->second);
      }
    } else {
      tree_->UnselectAll();
    }
    syncing_selection_ = false;
    last_sel_rev_ = scene_.SelectionRevision();
  }

  if (const uint64_t rename_uid = scene_.ConsumeRenameRequest()) {
    auto it = uid_to_item_.find(rename_uid);
    if (it != uid_to_item_.end() && it->second.IsOk()) {
      tree_->EnsureVisible(it->second);
      tree_->EditLabel(it->second);
    }
  }
}

void HierarchyPanel::CollectExpandedUids() {
  if (!tree_->GetRootItem().IsOk()) return;
  for (const auto& [uid, item] : uid_to_item_) {
    if (item.IsOk() && tree_->ItemHasChildren(item)) {
      if (tree_->IsExpanded(item)) expanded_uids_.insert(uid);
      else expanded_uids_.erase(uid);
    }
  }
}

bool HierarchyPanel::NodeMatchesFilter(const MapNode& node,
                                         const std::string& filter_lower) const {
  if (filter_lower.empty()) return true;
  if (ToLower(node.name).find(filter_lower) != std::string::npos) return true;
  for (const auto& c : node.children) {
    if (NodeMatchesFilter(*c, filter_lower)) return true;
  }
  return false;
}

void HierarchyPanel::PopulateChildren(const wxTreeItemId& parent_item,
                                      const MapNode& node,
                                      const std::string& filter_lower) {
  for (const auto& child : node.children) {
    if (!NodeMatchesFilter(*child, filter_lower)) continue;
    wxTreeItemId item =
        tree_->AppendItem(parent_item, wxString::FromUTF8(child->name), -1, -1,
                          new NodeItemData(child->uid));
    uid_to_item_[child->uid] = item;
    PopulateChildren(item, *child, filter_lower);
    if (!filter_lower.empty() || expanded_uids_.contains(child->uid) ||
        !expanded_uids_.contains(node.uid)) {
      if (tree_->ItemHasChildren(item)) {
        tree_->Expand(item);
      }
    }
  }
}

void HierarchyPanel::RebuildTree() {
  CollectExpandedUids();
  const bool first_populate = uid_to_item_.empty();
  const std::string filter_lower = ToLower(search_->GetValue().ToStdString());

  syncing_selection_ = true;
  tree_->Freeze();
  tree_->DeleteAllItems();
  uid_to_item_.clear();

  const MapNode* root = scene_.GetMap().GetRoot();
  if (root) {
    wxTreeItemId root_item =
        tree_->AddRoot(wxString::FromUTF8(root->name), -1, -1,
                       new NodeItemData(root->uid));
    uid_to_item_[root->uid] = root_item;
    if (first_populate) expanded_uids_.insert(root->uid);
    PopulateChildren(root_item, *root, filter_lower);
    tree_->Expand(root_item);
  }

  const uint64_t sel_uid = scene_.GetSelectedUid();
  if (sel_uid != 0) {
    if (auto it = uid_to_item_.find(sel_uid);
        it != uid_to_item_.end() && it->second.IsOk()) {
      tree_->SelectItem(it->second);
      tree_->EnsureVisible(it->second);
    }
  }
  tree_->Thaw();
  syncing_selection_ = false;

  last_map_rev_ = scene_.MapRevision();
  last_sel_rev_ = scene_.SelectionRevision();
  filter_dirty_ = false;
}

void HierarchyPanel::ShowContextMenu(const wxPoint& screen_pos, MapNode* node) {
  wxMenu menu;
  if (!node) {
    PopulateCreateNodeMenu(&menu, scene_, nullptr);
    PopupMenu(&menu, ScreenToClient(screen_pos));
    return;
  }

  auto* sub = new wxMenu();
  PopulateCreateNodeMenu(sub, scene_, node, nullptr, &menu);
  menu.AppendSubMenu(sub, "Create Child");
  menu.AppendSeparator();

  wxMenuItem* rename_item = menu.Append(wxID_ANY, "Rename\tF2");
  menu.Bind(
      wxEVT_MENU,
      [this, uid = node->uid](wxCommandEvent&) {
        if (auto it = uid_to_item_.find(uid);
            it != uid_to_item_.end() && it->second.IsOk()) {
          tree_->EditLabel(it->second);
        }
      },
      rename_item->GetId());

  if (!node->IsRoot()) {
    wxMenuItem* dup_item = menu.Append(wxID_ANY, "Duplicate\tCtrl+D");
    menu.Bind(
        wxEVT_MENU,
        [this, node](wxCommandEvent&) {
          scene_.Select(node);
          scene_.DuplicateSelected();
        },
        dup_item->GetId());

    const int index = node->IndexInParent();
    wxMenuItem* up_item = menu.Append(wxID_ANY, "Move Up");
    up_item->Enable(index > 0);
    menu.Bind(
        wxEVT_MENU,
        [this, node, index](wxCommandEvent&) {
          scene_.ReparentNode(node, node->parent, index - 1);
        },
        up_item->GetId());

    wxMenuItem* down_item = menu.Append(wxID_ANY, "Move Down");
    down_item->Enable(
        node->parent &&
        index + 1 < static_cast<int>(node->parent->children.size()));
    menu.Bind(
        wxEVT_MENU,
        [this, node, index](wxCommandEvent&) {
          scene_.ReparentNode(node, node->parent, index + 2);
        },
        down_item->GetId());

    menu.AppendSeparator();
    wxMenuItem* del_item = menu.Append(wxID_ANY, "Delete\tDel");
    menu.Bind(
        wxEVT_MENU,
        [this, node](wxCommandEvent&) {
          scene_.Select(node);
          scene_.DeleteSelected();
        },
        del_item->GetId());
  }

  PopupMenu(&menu, ScreenToClient(screen_pos));
}

}  // namespace Mercury::EditorUI
