#include "project_browser_panel.h"

#include <wx/clipbrd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "dialogs.h"
#include "dnd_payloads.h"
#include "editor_icons.h"
#include "os/process.h"
#include "project/project.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

class DirItemData : public wxTreeItemData {
 public:
  explicit DirItemData(fs::path p) : path(std::move(p)) {}
  fs::path path;
};

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool ShouldSkipDirName(const std::string& name) {
  return name.starts_with(".") || name == "build" ||
         name.starts_with("cmake-build") || name.starts_with("bazel-");
}

uint64_t NowMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

std::string ComputeDirSignature(const fs::path& root) {
  std::error_code ec;
  // FNV-1a over every path: cheaper than building a potentially huge string
  // of the whole project tree, which this used to do several times a second.
  uint64_t hash = 1469598103934665603ull;
  auto mix = [&hash](std::string_view text) {
    for (unsigned char c : text) {
      hash ^= c;
      hash *= 1099511628211ull;
    }
  };
  for (auto it = fs::recursive_directory_iterator(root, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    const std::string name = it->path().filename().string();
    if (it->is_directory(ec) && ShouldSkipDirName(name)) {
      it.disable_recursion_pending();
      continue;
    }
    mix(it->path().string());
    hash ^= '\n';
  }
  return std::to_string(hash);
}

}  // namespace

ProjectBrowserPanel::ProjectBrowserPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY), scene_(scene) {
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* bar = new wxPanel(this, wxID_ANY);
  auto* bar_sizer = new wxBoxSizer(wxHORIZONTAL);

  auto* create_btn = new wxButton(bar, wxID_ANY, "Create");
  ApplyButtonIconAndPadding(create_btn, EditorIcon::kCreate);
  bar_sizer->Add(create_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  path_label_ =
      new wxStaticText(bar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                       wxDefaultSize, wxST_ELLIPSIZE_START);
  path_label_->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  bar_sizer->Add(path_label_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

  search_ = new wxTextCtrl(bar, wxID_ANY, wxEmptyString, wxDefaultPosition,
                           wxSize(160, -1));
  search_->SetHint("Search files...");
  bar_sizer->Add(search_, 0, wxALIGN_CENTER_VERTICAL);

  auto* bar_wrap = new wxBoxSizer(wxVERTICAL);
  bar_wrap->Add(bar_sizer, 0, wxEXPAND | wxALL, 4);
  bar->SetSizer(bar_wrap);
  root->Add(bar, 0, wxEXPAND);

  splitter_ =
      new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_LIVE_UPDATE | wxSP_3DSASH);
  splitter_->SetMinimumPaneSize(110);

  dir_tree_ = new wxTreeCtrl(
      splitter_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE | wxBORDER_NONE);

  file_list_ =
      new wxListCtrl(splitter_, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
  file_list_->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 220);
  file_list_->InsertColumn(1, "Location / Status", wxLIST_FORMAT_LEFT, 160);

  splitter_->SplitVertically(dir_tree_, file_list_, 180);
  root->Add(splitter_, 1, wxEXPAND);
  SetSizer(root);

  create_btn->Bind(wxEVT_BUTTON, [this, create_btn](wxCommandEvent&) {
    auto project = scene_.GetProject();
    if (!project) return;
    const fs::path base =
        current_dir_.empty() ? project->GetRoot() : current_dir_;
    ShowCreateMenu(base, create_btn, wxPoint(0, create_btn->GetSize().y));
  });

  search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RebuildFileList(); });

  dir_tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& evt) {
    if (syncing_tree_) return;
    if (!evt.GetItem().IsOk()) return;
    auto* data =
        static_cast<DirItemData*>(dir_tree_->GetItemData(evt.GetItem()));
    if (data) {
      current_dir_ = data->path;
      RebuildFileList();
    }
  });

  dir_tree_->Bind(wxEVT_TREE_ITEM_MENU, [this](wxTreeEvent& evt) {
    if (!evt.GetItem().IsOk()) return;
    auto* data =
        static_cast<DirItemData*>(dir_tree_->GetItemData(evt.GetItem()));
    if (data) {
      dir_tree_->SelectItem(evt.GetItem());
      current_dir_ = data->path;
      RebuildFileList();
      ShowFolderContextMenu(data->path,
                            dir_tree_->ClientToScreen(evt.GetPoint()));
    }
  });

  file_list_->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
    const int total = std::max(220, file_list_->GetClientSize().x - 12);
    const int name_w = static_cast<int>(total * 0.62);
    file_list_->SetColumnWidth(0, name_w);
    file_list_->SetColumnWidth(1, std::max(80, total - name_w));
    evt.Skip();
  });

  file_list_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& evt) {
    auto project = scene_.GetProject();
    const long idx = evt.GetIndex();
    if (!project || idx < 0 ||
        static_cast<size_t>(idx) >= visible_files_.size()) {
      return;
    }
    const fs::path& p = visible_files_[idx];
    if (ToLower(p.extension().string()) == kAssetExtension) {
      scene_.SelectAsset(project->ToResPath(p));
      last_sel_rev_ = scene_.SelectionRevision();
    }
  });

  file_list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& evt) {
    auto project = scene_.GetProject();
    const long idx = evt.GetIndex();
    if (!project || idx < 0 ||
        static_cast<size_t>(idx) >= visible_files_.size()) {
      return;
    }
    const fs::path& p = visible_files_[idx];
    const std::string ext = ToLower(p.extension().string());
    const std::string res_path = project->ToResPath(p);
    if (ext == kMapExtension) {
      if (res_path != scene_.GetMapPath()) {
        if (scene_.IsDirty()) scene_.SaveMap();
        scene_.OpenMap(res_path);
      }
    } else if (ext == kUxmlExtension) {
      scene_.OpenInUIBuilder(res_path);
    } else if (ext == kInputActionsExtension) {
      scene_.OpenInInputActions(res_path);
    } else if (ext != kAssetExtension) {
      scene_.OpenInExternalEditor(p);
    }
  });

  file_list_->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK, [this](wxListEvent& evt) {
    const long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= visible_files_.size()) return;
    ShowFileContextMenu(visible_files_[idx],
                        file_list_->ClientToScreen(evt.GetPoint()));
  });

  file_list_->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& evt) {
    int flags = 0;
    const long idx = file_list_->HitTest(evt.GetPosition(), flags);
    if (idx < 0 && !current_dir_.empty()) {
      ShowFolderContextMenu(current_dir_,
                            file_list_->ClientToScreen(evt.GetPosition()));
    } else {
      evt.Skip();
    }
  });

  file_list_->Bind(wxEVT_LIST_BEGIN_DRAG, [this](wxListEvent& evt) {
    auto project = scene_.GetProject();
    const long idx = evt.GetIndex();
    if (!project || idx < 0 ||
        static_cast<size_t>(idx) >= visible_files_.size()) {
      return;
    }
    const fs::path& abs_path = visible_files_[idx];
    const std::string res_path = project->ToResPath(abs_path);
    std::unique_ptr<wxDataObjectComposite> drag_obj(
        MakeResourceDragObject(res_path, abs_path));
    wxDropSource source(*drag_obj, file_list_);
    source.DoDragDrop(wxDrag_CopyOnly);
  });

  // Allow dropping files from OS desktop/file manager directly into folders.
  dir_tree_->SetDropTarget(new CompositeDropTarget(
      [this](wxCoord x, wxCoord y, const wxArrayString& files) {
        auto project = scene_.GetProject();
        if (!project) return false;
        fs::path dest =
            current_dir_.empty() ? project->GetRoot() : current_dir_;
        int flags = 0;
        const wxTreeItemId hit = dir_tree_->HitTest(wxPoint(x, y), flags);
        if (hit.IsOk()) {
          if (auto* data =
                  static_cast<DirItemData*>(dir_tree_->GetItemData(hit))) {
            dest = data->path;
          }
        }
        return CopyExternalFilesInto(dest, files);
      },
      nullptr));

  file_list_->SetDropTarget(new CompositeDropTarget(
      [this](wxCoord, wxCoord, const wxArrayString& files) {
        auto project = scene_.GetProject();
        if (!project) return false;
        fs::path dest =
            current_dir_.empty() ? project->GetRoot() : current_dir_;
        return CopyExternalFilesInto(dest, files);
      },
      nullptr));

  RefreshAll(true);
}

void ProjectBrowserPanel::SyncFromController() {
  auto project = scene_.GetProject();
  if (!project) {
    if (!last_project_root_.empty()) {
      last_project_root_.clear();
      current_dir_.clear();
      dir_tree_->DeleteAllItems();
      file_list_->DeleteAllItems();
      path_label_->SetLabel(wxEmptyString);
    }
    return;
  }

  const bool project_changed = project->GetRoot() != last_project_root_;
  const uint64_t now = NowMs();
  bool fs_changed = false;
  if (project_changed || now - last_scan_ms_ >= 1000) {
    last_scan_ms_ = now;
    const std::string sig = ComputeDirSignature(project->GetRoot());
    if (sig != last_signature_) {
      last_signature_ = sig;
      fs_changed = true;
    }
  }

  // MapRevision changes on every transform edit (including every tick of a
  // handle drag), which must not rebuild the file list. The list only depends
  // on the filesystem, the current selection and Lore lock state.
  const bool rev_changed = scene_.SelectionRevision() != last_sel_rev_ ||
                           scene_.LoreRef().StateRevision() != last_lore_rev_;
  if (project_changed || fs_changed) {
    RefreshAll(true);
  } else if (rev_changed) {
    RebuildFileList();
  }
}

void ProjectBrowserPanel::RefreshAll(bool force_tree) {
  auto project = scene_.GetProject();
  if (!project) return;
  if (last_project_root_ != project->GetRoot()) {
    last_project_root_ = project->GetRoot();
    current_dir_ = last_project_root_;
    last_signature_ = ComputeDirSignature(last_project_root_);
  }
  if (current_dir_.empty() || !fs::exists(current_dir_)) {
    current_dir_ = project->GetRoot();
  }
  path_label_->SetLabel(wxString::FromUTF8(project->ToResPath(current_dir_)));
  if (force_tree) RebuildFolderTree();
  RebuildFileList();
}

void ProjectBrowserPanel::RebuildFolderTree() {
  auto project = scene_.GetProject();
  if (!project) return;
  syncing_tree_ = true;
  dir_tree_->Freeze();
  dir_tree_->DeleteAllItems();
  path_to_tree_.clear();

  const fs::path root_path = project->GetRoot();
  wxTreeItemId root_item =
      dir_tree_->AddRoot(wxString::FromUTF8(project->GetName() + "/"), -1, -1,
                         new DirItemData(root_path));
  path_to_tree_[root_path.string()] = root_item;
  PopulateFolderChildren(root_item, root_path);
  dir_tree_->Expand(root_item);

  if (auto it = path_to_tree_.find(current_dir_.string());
      it != path_to_tree_.end()) {
    dir_tree_->SelectItem(it->second);
    dir_tree_->EnsureVisible(it->second);
  } else {
    dir_tree_->SelectItem(root_item);
  }
  dir_tree_->Thaw();
  syncing_tree_ = false;
}

void ProjectBrowserPanel::PopulateFolderChildren(
    const wxTreeItemId& parent_item, const fs::path& dir) {
  std::vector<fs::path> folders;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (entry.is_directory(ec) && !ShouldSkipDirName(name)) {
      folders.push_back(entry.path());
    }
  }
  std::sort(
      folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
        return ToLower(a.filename().string()) < ToLower(b.filename().string());
      });
  for (const fs::path& f : folders) {
    wxTreeItemId item = dir_tree_->AppendItem(
        parent_item, wxString::FromUTF8(f.filename().string() + "/"), -1, -1,
        new DirItemData(f));
    path_to_tree_[f.string()] = item;
    PopulateFolderChildren(item, f);
  }
}

void ProjectBrowserPanel::RebuildFileList() {
  auto project = scene_.GetProject();
  if (!project) return;
  const fs::path target_dir =
      current_dir_.empty() ? project->GetRoot() : current_dir_;
  path_label_->SetLabel(wxString::FromUTF8(project->ToResPath(target_dir)));

  const std::string needle = ToLower(search_->GetValue().ToStdString());
  std::vector<fs::path> found;
  std::error_code ec;
  if (!needle.empty()) {
    for (auto it = fs::recursive_directory_iterator(project->GetRoot(), ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      const std::string name = it->path().filename().string();
      if (it->is_directory(ec)) {
        if (ShouldSkipDirName(name)) it.disable_recursion_pending();
        continue;
      }
      if (ToLower(name).find(needle) != std::string::npos) {
        found.push_back(it->path());
      }
    }
  } else {
    for (const auto& entry : fs::directory_iterator(target_dir, ec)) {
      const std::string name = entry.path().filename().string();
      if (name.starts_with(".")) continue;
      if (!entry.is_directory(ec)) found.push_back(entry.path());
    }
  }

  std::sort(
      found.begin(), found.end(), [](const fs::path& a, const fs::path& b) {
        return ToLower(a.filename().string()) < ToLower(b.filename().string());
      });

  visible_files_ = std::move(found);
  const bool lore_repo = scene_.LoreRef().IsRepository();
  const std::string selected_asset = scene_.GetSelectedAsset();
  const std::string map_path = scene_.GetMapPath();
  file_list_->Freeze();
  file_list_->DeleteAllItems();
  for (size_t i = 0; i < visible_files_.size(); ++i) {
    const fs::path& p = visible_files_[i];
    const std::string name = p.filename().string();
    const std::string ext = ToLower(p.extension().string());
    const std::string res_path = project->ToResPath(p);
    const bool locked = lore_repo && scene_.LoreRef().IsLocked(p);
    const bool selected = res_path == selected_asset || res_path == map_path;

    wxString label = wxString::FromUTF8(name);
    if (locked) label += "  [locked]";
    const long row = file_list_->InsertItem(static_cast<long>(i), label);

    std::string meta = ext.empty() ? "file" : ext.substr(1);
    if (!needle.empty()) meta = project->ToResPath(p.parent_path());
    if (locked) meta += " (locked)";
    file_list_->SetItem(row, 1, wxString::FromUTF8(meta));
    if (selected) {
      file_list_->SetItemState(row, wxLIST_STATE_SELECTED,
                               wxLIST_STATE_SELECTED);
    }
  }
  file_list_->Thaw();

  last_sel_rev_ = scene_.SelectionRevision();
  last_lore_rev_ = scene_.LoreRef().StateRevision();
}

bool ProjectBrowserPanel::CopyExternalFilesInto(const fs::path& dest_dir,
                                                const wxArrayString& files) {
  auto project = scene_.GetProject();
  if (!project || files.IsEmpty()) return false;
  std::error_code ec;
  fs::create_directories(dest_dir, ec);
  bool copied_any = false;
  std::string single_obj_res;
  for (const wxString& f : files) {
    const fs::path src = f.ToStdString();
    if (!fs::is_regular_file(src, ec)) continue;
    const fs::path dst = dest_dir / src.filename();
    if (fs::equivalent(src, dst, ec)) continue;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
      copied_any = true;
      scene_.ConsoleRef().Info("Imported file into project: " +
                               project->ToResPath(dst));
      if (ToLower(dst.extension().string()) == ".obj") {
        single_obj_res = project->ToResPath(dst);
      }
    } else {
      scene_.ConsoleRef().Error("Copy failed: " + ec.message());
    }
  }
  if (copied_any) {
    RefreshAll(true);
    if (!single_obj_res.empty()) {
      scene_.SelectAsset(single_obj_res);
    }
  }
  return copied_any;
}

void ProjectBrowserPanel::ShowCreateMenu(const fs::path& target_dir,
                                         wxWindow* anchor, const wxPoint& pos) {
  auto project = scene_.GetProject();
  if (!project) return;
  wxMenu menu;
  const fs::path asset_dir = target_dir == project->GetRoot()
                                 ? project->GetRoot() / "assets"
                                 : target_dir;
  const fs::path ui_dir =
      target_dir == project->GetRoot() ? project->GetRoot() / "ui" : target_dir;
  const fs::path map_dir = target_dir == project->GetRoot()
                               ? project->GetRoot() / "maps"
                               : target_dir;
  const fs::path input_dir = target_dir == project->GetRoot()
                                 ? project->GetRoot() / "input"
                                 : target_dir;

  auto bind = [&](const char* label, std::function<void()> fn) {
    wxMenuItem* it = menu.Append(wxID_ANY, label);
    menu.Bind(wxEVT_MENU, [fn](wxCommandEvent&) { fn(); }, it->GetId());
  };

  bind("Script...", [this]() { HandleCreateScript(); });
  bind("Map...", [this, map_dir]() { HandleCreateMap(map_dir); });
  bind("Folder...", [this, target_dir]() { HandleCreateFolder(target_dir); });
  menu.AppendSeparator();
  bind("Material", [this, asset_dir]() {
    scene_.CreateAsset(asset_dir, "Material");
    RefreshAll(true);
  });
  bind("Physics Material", [this, asset_dir]() {
    scene_.CreateAsset(asset_dir, "PhysicsMaterial");
    RefreshAll(true);
  });
  bind("Input Actions", [this, input_dir]() {
    scene_.CreateInputActionsFile(input_dir);
    RefreshAll(true);
  });
  menu.AppendSeparator();
  auto* ui_sub = new wxMenu();
  auto bind_sub = [&](const char* label, std::function<void()> fn) {
    wxMenuItem* it = ui_sub->Append(wxID_ANY, label);
    menu.Bind(wxEVT_MENU, [fn](wxCommandEvent&) { fn(); }, it->GetId());
  };
  bind_sub("UI Document (.uxml)", [this, ui_dir]() {
    std::error_code ec;
    fs::create_directories(ui_dir, ec);
    scene_.CreateUIFile(ui_dir, "uxml");
    RefreshAll(true);
  });
  bind_sub("Style Sheet (.uss)", [this, ui_dir]() {
    std::error_code ec;
    fs::create_directories(ui_dir, ec);
    scene_.CreateUIFile(ui_dir, "uss");
    RefreshAll(true);
  });
  bind_sub("Panel Settings", [this, ui_dir]() {
    std::error_code ec;
    fs::create_directories(ui_dir, ec);
    scene_.CreateAsset(ui_dir, "PanelSettings");
    RefreshAll(true);
  });
  menu.AppendSubMenu(ui_sub, "UI Toolkit");

  anchor->PopupMenu(&menu, pos);
}

void ProjectBrowserPanel::ShowFolderContextMenu(const fs::path& dir,
                                                const wxPoint& screen_pos) {
  auto project = scene_.GetProject();
  if (!project) return;
  wxMenu menu;
  auto bind = [&](const char* label, std::function<void()> fn) {
    wxMenuItem* it = menu.Append(wxID_ANY, label);
    menu.Bind(wxEVT_MENU, [fn](wxCommandEvent&) { fn(); }, it->GetId());
  };
  bind("New Folder...", [this, dir]() { HandleCreateFolder(dir); });
  bind("New Material", [this, dir]() {
    scene_.CreateAsset(dir, "Material");
    RefreshAll(true);
  });
  bind("New Physics Material", [this, dir]() {
    scene_.CreateAsset(dir, "PhysicsMaterial");
    RefreshAll(true);
  });
  bind("New Input Actions", [this, dir]() {
    scene_.CreateInputActionsFile(dir);
    RefreshAll(true);
  });
  bind("New UI Document", [this, dir]() {
    scene_.CreateUIFile(dir, "uxml");
    RefreshAll(true);
  });
  bind("New Style Sheet", [this, dir]() {
    scene_.CreateUIFile(dir, "uss");
    RefreshAll(true);
  });
  bind("New Panel Settings", [this, dir]() {
    scene_.CreateAsset(dir, "PanelSettings");
    RefreshAll(true);
  });
  bind("New Map...", [this, dir]() { HandleCreateMap(dir); });
  menu.AppendSeparator();
  bind("Open Folder", [dir]() { os::OpenWithDefaultApp(dir); });
  if (dir != project->GetRoot()) {
    menu.AppendSeparator();
    bind("Delete...", [this, dir]() { HandleDeletePath(dir); });
  }
  PopupMenu(&menu, ScreenToClient(screen_pos));
}

void ProjectBrowserPanel::ShowFileContextMenu(const fs::path& file,
                                              const wxPoint& screen_pos) {
  auto project = scene_.GetProject();
  if (!project) return;
  const std::string ext = ToLower(file.extension().string());
  const std::string res_path = project->ToResPath(file);
  const bool locked = scene_.LoreRef().IsLocked(file);

  wxMenu menu;
  auto bind = [&](const char* label, std::function<void()> fn,
                  bool enabled = true) {
    wxMenuItem* it = menu.Append(wxID_ANY, label);
    it->Enable(enabled);
    menu.Bind(wxEVT_MENU, [fn](wxCommandEvent&) { fn(); }, it->GetId());
  };

  if (ext == kMapExtension) {
    bind("Open Map", [this, res_path]() {
      if (scene_.IsDirty()) scene_.SaveMap();
      scene_.OpenMap(res_path);
    });
  }
  if (ext == kUxmlExtension) {
    bind("Open in UI Builder",
         [this, res_path]() { scene_.OpenInUIBuilder(res_path); });
  }
  if (ext == kInputActionsExtension) {
    bind("Open in Input Actions",
         [this, res_path]() { scene_.OpenInInputActions(res_path); });
  }
  if (ext == ".obj") {
    bind(
        "Import into Scene",
        [this, res_path]() { scene_.ImportModel(res_path); },
        !scene_.IsModelPreloading());
  }
  bind("Open in External Editor",
       [this, file]() { scene_.OpenInExternalEditor(file); });
  bind("Copy res:// Path", [res_path]() {
    if (wxTheClipboard->Open()) {
      wxTheClipboard->SetData(
          new wxTextDataObject(wxString::FromUTF8(res_path)));
      wxTheClipboard->Close();
    }
  });
  if (scene_.LoreRef().IsRepository()) {
    menu.AppendSeparator();
    if (locked) {
      bind("Release Lore Lock",
           [this, file]() { scene_.LoreRef().Unlock(file); });
    } else {
      bind("Lock with Lore", [this, file]() { scene_.LoreRef().Lock(file); });
    }
  }
  menu.AppendSeparator();
  bind(
      "Delete...", [this, file]() { HandleDeletePath(file); },
      res_path != scene_.GetMapPath());

  PopupMenu(&menu, ScreenToClient(screen_pos));
}

void ProjectBrowserPanel::HandleCreateScript() {
  if (auto cls = ShowNewScriptDialog(this)) {
    scene_.CreateScript(*cls);
    RefreshAll(true);
  }
}

void ProjectBrowserPanel::HandleCreateMap(const fs::path& dir) {
  auto project = scene_.GetProject();
  if (!project) return;
  if (auto name = PromptTextDialog(this, "New Map", "Map Name:")) {
    std::string filename = *name;
    if (!filename.ends_with(kMapExtension)) filename += kMapExtension;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / filename;
    const std::string res_path = project->ToResPath(file);
    if (scene_.IsDirty()) scene_.SaveMap();
    scene_.SaveNewMapAs(res_path);
    scene_.OpenMap(res_path);
    RefreshAll(true);
  }
}

void ProjectBrowserPanel::HandleCreateFolder(const fs::path& dir) {
  if (auto name = PromptTextDialog(this, "New Folder", "Folder Name:")) {
    scene_.CreateFolder(dir, *name);
    RefreshAll(true);
  }
}

void ProjectBrowserPanel::HandleDeletePath(const fs::path& target) {
  if (ShowDeleteConfirmDialog(this, target)) {
    scene_.DeleteProjectPath(target);
    if (current_dir_ == target) current_dir_ = target.parent_path();
    RefreshAll(true);
  }
}

}  // namespace Mercury::EditorUI
