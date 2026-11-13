#include "input_actions_panel.h"

#include <wx/clipbrd.h>
#include <wx/menu.h>
#include <wx/statline.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <sstream>

#include "dialogs.h"
#include "editor_icons.h"
#include "input/input_actions.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace {

class SelectionItemData : public wxTreeItemData {
 public:
  explicit SelectionItemData(InputActionsWindow::Selection sel) : sel(sel) {}
  InputActionsWindow::Selection sel;
};

int WxKeyToGlfwKey(int key) {
  if (key >= 'A' && key <= 'Z') return key;
  if (key >= 'a' && key <= 'z') return std::toupper(key);
  if (key >= '0' && key <= '9') return key;
  if (key >= WXK_F1 && key <= WXK_F12) return 290 + (key - WXK_F1);
  switch (key) {
    case WXK_SPACE:
      return 32;
    case '\'':
      return 39;
    case ',':
      return 44;
    case '-':
      return 45;
    case '.':
      return 46;
    case '/':
      return 47;
    case ';':
      return 59;
    case '=':
      return 61;
    case '[':
      return 91;
    case '\\':
      return 92;
    case ']':
      return 93;
    case '`':
      return 96;
    case WXK_ESCAPE:
      return 256;
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
      return 257;
    case WXK_TAB:
      return 258;
    case WXK_BACK:
      return 259;
    case WXK_INSERT:
      return 260;
    case WXK_DELETE:
      return 261;
    case WXK_RIGHT:
      return 262;
    case WXK_LEFT:
      return 263;
    case WXK_DOWN:
      return 264;
    case WXK_UP:
      return 265;
    case WXK_PAGEUP:
      return 266;
    case WXK_PAGEDOWN:
      return 267;
    case WXK_HOME:
      return 268;
    case WXK_END:
      return 269;
    case WXK_CAPITAL:
      return 280;
    case WXK_SCROLL:
      return 281;
    case WXK_NUMLOCK:
      return 282;
    case WXK_PAUSE:
      return 284;
    case WXK_SHIFT:
      return 340;
    case WXK_CONTROL:
      return 341;
    case WXK_ALT:
      return 342;
    case WXK_WINDOWS_LEFT:
      return 343;
    case WXK_WINDOWS_RIGHT:
      return 347;
    default:
      return -1;
  }
}

std::string ActionSummary(const InputActionDesc& act) {
  std::string s = act.name;
  std::string tags;
  if (act.type == InputAction::Type::kValue) {
    tags = act.vector2 ? "Vector2" : "Value";
  } else if (act.type == InputAction::Type::kPassThrough) {
    tags = act.vector2 ? "Pass/Vec2" : "Pass";
  }
  if (!tags.empty()) s += "  [" + tags + "]";
  return s;
}

std::string CompositeSummary(const InputActionDesc::Composite2D& comp) {
  auto join_part = [](const std::vector<std::string>& paths) {
    if (paths.empty()) return std::string("_");
    std::string p = paths.front();
    auto slash = p.find('/');
    if (slash != std::string::npos) p = p.substr(slash + 1);
    return p;
  };
  return "2D Vector (" + join_part(comp.up) + "/" + join_part(comp.down) + "/" +
         join_part(comp.left) + "/" + join_part(comp.right) + ")";
}

wxStaticText* MakeSectionHeader(wxWindow* parent, const wxString& title) {
  auto* lbl = new wxStaticText(parent, wxID_ANY, title);
  lbl->SetFont(HeaderFont());
  return lbl;
}

}  // namespace

InputActionsPanel::InputActionsPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY), scene_(scene) {
  BuildLayout();
  Bind(wxEVT_CHAR_HOOK, &InputActionsPanel::OnCharHook, this);
  RefreshAll(true);
}

void InputActionsPanel::BuildLayout() {
  auto* root = new wxBoxSizer(wxVERTICAL);

  // Row 1: File, Save, Undo/Redo, Use as Project Actions, Pop Out
  auto* bar1 = new wxPanel(this);
  auto* bar1_s = new wxBoxSizer(wxHORIZONTAL);

  auto* file_lbl = new wxStaticText(bar1, wxID_ANY, "File:");
  file_choice_ = new wxChoice(bar1, wxID_ANY);
  file_choice_->SetMinSize(wxSize(210, -1));
  file_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    int idx = file_choice_->GetSelection();
    if (idx >= 0 && static_cast<size_t>(idx) < minput_files_.size()) {
      scene_.InputActionsRef().Open(minput_files_[idx]);
      RefreshAll(true);
    }
  });

  new_file_btn_ = new wxButton(bar1, wxID_ANY, "New .minput");
  ApplyButtonIconAndPadding(new_file_btn_, EditorIcon::kNewFile);
  new_file_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto proj = scene_.GetProject();
    if (!proj) return;
    scene_.CreateInputActionsFile(proj->GetRoot());
    RefreshFileChoices();
    RefreshAll(true);
  });

  save_btn_ = new wxButton(bar1, wxID_ANY, "Save");
  ApplyButtonIconAndPadding(save_btn_, EditorIcon::kSave);
  save_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().Save();
    RefreshAll(true);
  });

  undo_btn_ = new wxButton(bar1, wxID_ANY, "Undo");
  ApplyButtonIconAndPadding(undo_btn_, EditorIcon::kUndo);
  undo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().Undo();
    RefreshAll(true);
  });

  redo_btn_ = new wxButton(bar1, wxID_ANY, "Redo");
  ApplyButtonIconAndPadding(redo_btn_, EditorIcon::kRedo);
  redo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().Redo();
    RefreshAll(true);
  });

  project_actions_btn_ = new wxButton(bar1, wxID_ANY, "Use as Project Actions");
  ApplyButtonPadding(project_actions_btn_, 10);
  project_actions_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().SetAsProjectActions();
    RefreshTopBar();
  });

  project_actions_lbl_ =
      new wxStaticText(bar1, wxID_ANY, "Project Actions (Active)");
  project_actions_lbl_->SetFont(bar1->GetFont().Bold());

  popout_btn_ = new wxButton(bar1, wxID_ANY, "Pop Out");
  ApplyButtonIconAndPadding(popout_btn_, EditorIcon::kPopOut);
  popout_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (on_popout_) on_popout_(!popped_out_);
  });

  bar1_s->Add(file_lbl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  bar1_s->Add(file_choice_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  bar1_s->Add(new_file_btn_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  bar1_s->AddSpacer(8);
  bar1_s->Add(save_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bar1_s->Add(undo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bar1_s->Add(redo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  bar1_s->Add(project_actions_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bar1_s->Add(project_actions_lbl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bar1_s->AddStretchSpacer();
  bar1_s->Add(popout_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bar1->SetSizer(bar1_s);
  root->Add(bar1, 0, wxEXPAND | wxALL, 2);

  // Row 2: AngelScript codegen settings
  codegen_bar_ = new wxPanel(this);
  auto* cg_s = new wxBoxSizer(wxHORIZONTAL);

  gen_cb_ = new wxCheckBox(codegen_bar_, wxID_ANY, "Generate AngelScript");
  gen_cb_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& evt) {
    if (syncing_) return;
    scene_.InputActionsRef().SetGenerateHeader(evt.IsChecked());
    RefreshTopBar();
  });

  auto* cls_lbl = new wxStaticText(codegen_bar_, wxID_ANY, "Class:");
  class_edit_ = new wxTextCtrl(codegen_bar_, wxID_ANY, "", wxDefaultPosition,
                               wxSize(130, -1));
  class_edit_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
    scene_.InputActionsRef().BeginEdit();
    evt.Skip();
  });
  class_edit_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    if (syncing_) return;
    scene_.InputActionsRef().SetClassName(class_edit_->GetValue().ToStdString(),
                                          true);
  });
  class_edit_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
    scene_.InputActionsRef().CommitEdit();
    RefreshTopBar();
    evt.Skip();
  });

  auto* hdr_lbl = new wxStaticText(codegen_bar_, wxID_ANY, "Header:");
  header_edit_ = new wxTextCtrl(codegen_bar_, wxID_ANY, "", wxDefaultPosition,
                                wxSize(240, -1));
  header_edit_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
    scene_.InputActionsRef().BeginEdit();
    evt.Skip();
  });
  header_edit_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    if (syncing_) return;
    scene_.InputActionsRef().SetHeaderPath(
        header_edit_->GetValue().ToStdString(), true);
  });
  header_edit_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
    scene_.InputActionsRef().CommitEdit();
    RefreshTopBar();
    evt.Skip();
  });

  write_header_btn_ = new wxButton(codegen_bar_, wxID_ANY, "Write Header");
  ApplyButtonPadding(write_header_btn_, 10);
  write_header_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().WriteHeader();
    RefreshTopBar();
  });

  header_err_lbl_ = new wxStaticText(codegen_bar_, wxID_ANY, "");
  header_err_lbl_->SetFont(codegen_bar_->GetFont().Bold());

  cg_s->Add(gen_cb_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
  cg_s->AddSpacer(8);
  cg_s->Add(cls_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  cg_s->Add(class_edit_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  cg_s->Add(hdr_lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  cg_s->Add(header_edit_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  cg_s->Add(write_header_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  cg_s->Add(header_err_lbl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  codegen_bar_->SetSizer(cg_s);
  root->Add(codegen_bar_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2);

  // Empty banner
  empty_banner_ = new wxPanel(this);
  auto* eb_s = new wxBoxSizer(wxVERTICAL);
  auto* eb_lbl = new wxStaticText(
      empty_banner_, wxID_ANY,
      "Open a .minput file from Project Browser or the File selector above, "
      "or click + New .minput.");
  eb_lbl->SetForegroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  eb_s->AddStretchSpacer();
  eb_s->Add(eb_lbl, 0, wxALIGN_CENTER_HORIZONTAL);
  eb_s->AddStretchSpacer();
  empty_banner_->SetSizer(eb_s);
  root->Add(empty_banner_, 1, wxEXPAND);

  // 3-pane splitter: Maps | (Actions | Properties)
  left_split_ =
      new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxSP_LIVE_UPDATE | wxSP_3DSASH);
  left_split_->SetMinimumPaneSize(170);

  // Left: Action Maps
  auto* maps_pane = new wxPanel(left_split_);
  auto* maps_s = new wxBoxSizer(wxVERTICAL);
  auto* maps_hdr = new wxBoxSizer(wxHORIZONTAL);
  maps_hdr->Add(MakeSectionHeader(maps_pane, "Action Maps"), 0,
                wxALIGN_CENTER_VERTICAL);
  maps_hdr->AddStretchSpacer();
  add_map_btn_ =
      new wxButton(maps_pane, wxID_ANY, "+", wxDefaultPosition, wxSize(26, 22));
  rename_map_btn_ = new wxButton(maps_pane, wxID_ANY, "Ren");
  ApplyButtonPadding(rename_map_btn_, 8);
  del_map_btn_ =
      new wxButton(maps_pane, wxID_ANY, "-", wxDefaultPosition, wxSize(26, 22));
  maps_hdr->Add(add_map_btn_, 0, wxRIGHT, 2);
  maps_hdr->Add(rename_map_btn_, 0, wxRIGHT, 2);
  maps_hdr->Add(del_map_btn_, 0);
  maps_s->Add(maps_hdr, 0, wxEXPAND | wxALL, 4);

  maps_list_ = new wxListBox(maps_pane, wxID_ANY);
  maps_s->Add(maps_list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  maps_pane->SetSizer(maps_s);

  add_map_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().AddMap();
    RefreshAll(true);
  });
  rename_map_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string cur = scene_.InputActionsRef().SelectedMap();
    if (cur.empty()) return;
    auto renamed =
        PromptTextDialog(this, "Rename Action Map", "Map name:", cur);
    if (renamed.has_value() && !renamed->empty()) {
      scene_.InputActionsRef().RenameMap(cur, *renamed);
      RefreshAll(true);
    }
  });
  del_map_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const std::string cur = scene_.InputActionsRef().SelectedMap();
    if (cur.empty()) return;
    scene_.InputActionsRef().DeleteMap(cur);
    RefreshAll(true);
  });
  maps_list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) {
    if (syncing_) return;
    int idx = maps_list_->GetSelection();
    if (idx != wxNOT_FOUND) {
      scene_.InputActionsRef().SelectMap(idx);
      RefreshActionsTree();
      SchedulePropsRefresh();
    }
  });
  maps_list_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) {
    const std::string cur = scene_.InputActionsRef().SelectedMap();
    if (cur.empty()) return;
    auto renamed =
        PromptTextDialog(this, "Rename Action Map", "Map name:", cur);
    if (renamed.has_value() && !renamed->empty()) {
      scene_.InputActionsRef().RenameMap(cur, *renamed);
      RefreshAll(true);
    }
  });

  // Right splitter: Actions Tree | Properties
  right_split_ =
      new wxSplitterWindow(left_split_, wxID_ANY, wxDefaultPosition,
                           wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
  right_split_->SetMinimumPaneSize(220);

  // Middle: Actions & Bindings
  auto* act_pane = new wxPanel(right_split_);
  auto* act_s = new wxBoxSizer(wxVERTICAL);
  auto* act_hdr = new wxBoxSizer(wxHORIZONTAL);
  act_hdr->Add(MakeSectionHeader(act_pane, "Actions & Bindings"), 0,
               wxALIGN_CENTER_VERTICAL);
  act_hdr->AddStretchSpacer();
  act_s->Add(act_hdr, 0, wxEXPAND | wxALL, 4);

  auto* act_btns = new wxBoxSizer(wxHORIZONTAL);
  add_action_btn_ = new wxButton(act_pane, wxID_ANY, "Action");
  ApplyButtonIconAndPadding(add_action_btn_, EditorIcon::kCreate, 8, 13, 5);
  add_binding_btn_ = new wxButton(act_pane, wxID_ANY, "Binding");
  ApplyButtonIconAndPadding(add_binding_btn_, EditorIcon::kCreate, 8, 13, 5);
  add_comp_btn_ = new wxButton(act_pane, wxID_ANY, "2DVec");
  ApplyButtonIconAndPadding(add_comp_btn_, EditorIcon::kCreate, 8, 13, 5);
  dup_btn_ = new wxButton(act_pane, wxID_ANY, "Dup");
  ApplyButtonIconAndPadding(dup_btn_, EditorIcon::kDuplicate, 8, 13, 5);
  del_btn_ = new wxButton(act_pane, wxID_ANY, "Del");
  ApplyButtonIconAndPadding(del_btn_, EditorIcon::kDelete, 8, 13, 5);
  act_btns->Add(add_action_btn_, 0, wxRIGHT, 2);
  act_btns->Add(add_binding_btn_, 0, wxRIGHT, 2);
  act_btns->Add(add_comp_btn_, 0, wxRIGHT, 2);
  act_btns->Add(dup_btn_, 0, wxRIGHT, 2);
  act_btns->Add(del_btn_, 0);
  act_s->Add(act_btns, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

  actions_tree_ = new wxTreeCtrl(
      act_pane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_HIDE_ROOT | wxTR_SINGLE);
  act_s->Add(actions_tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  act_pane->SetSizer(act_s);

  add_action_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto& ia = scene_.InputActionsRef();
    ia.AddAction(ia.SelectedMap());
    RefreshAll(true);
  });
  add_binding_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto& ia = scene_.InputActionsRef();
    if (ia.CurrentSelection().action >= 0) {
      ia.AddBinding(ia.CurrentSelection().action);
      RefreshAll(true);
    }
  });
  add_comp_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto& ia = scene_.InputActionsRef();
    if (ia.CurrentSelection().action >= 0) {
      ia.AddComposite(ia.CurrentSelection().action);
      RefreshAll(true);
    }
  });
  dup_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().DuplicateSelected();
    RefreshAll(true);
  });
  del_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.InputActionsRef().DeleteSelected();
    RefreshAll(true);
  });
  actions_tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& evt) {
    if (syncing_) return;
    auto* data = static_cast<SelectionItemData*>(
        actions_tree_->GetItemData(evt.GetItem()));
    if (data) {
      scene_.InputActionsRef().Select(data->sel);
      SchedulePropsRefresh();
    }
  });

  // Right: Properties
  auto* prop_pane = new wxPanel(right_split_);
  auto* prop_root = new wxBoxSizer(wxVERTICAL);
  prop_root->Add(MakeSectionHeader(prop_pane, "Properties"), 0, wxALL, 6);
  prop_root->Add(new wxStaticLine(prop_pane), 0, wxEXPAND | wxLEFT | wxRIGHT,
                 6);

  props_scroll_ = new wxScrolledWindow(prop_pane, wxID_ANY);
  props_scroll_->SetScrollRate(0, 14);
  props_sizer_ = new wxBoxSizer(wxVERTICAL);
  props_scroll_->SetSizer(props_sizer_);
  prop_root->Add(props_scroll_, 1, wxEXPAND | wxALL, 6);
  prop_pane->SetSizer(prop_root);

  right_split_->SplitVertically(act_pane, prop_pane, 310);
  right_split_->SetSashGravity(0.45);
  left_split_->SplitVertically(maps_pane, right_split_, 200);
  left_split_->SetSashGravity(0.2);

  root->Add(left_split_, 1, wxEXPAND);
  SetSizer(root);
}

void InputActionsPanel::RefreshFileChoices() {
  minput_files_ = scene_.ListProjectFiles({".minput"});
  auto& ia = scene_.InputActionsRef();

  syncing_ = true;
  file_choice_->Clear();
  int sel = wxNOT_FOUND;
  for (size_t i = 0; i < minput_files_.size(); ++i) {
    file_choice_->Append(wxString::FromUTF8(minput_files_[i]));
    if (minput_files_[i] == ia.path()) sel = static_cast<int>(i);
  }
  if (sel != wxNOT_FOUND) file_choice_->SetSelection(sel);
  syncing_ = false;
}

void InputActionsPanel::RefreshTopBar() {
  auto& ia = scene_.InputActionsRef();
  const bool open = ia.IsOpen();

  save_btn_->Enable(open && ia.IsDirty());
  undo_btn_->Enable(open && ia.CanUndo());
  redo_btn_->Enable(open && ia.CanRedo());

  const bool is_proj = open && ia.IsProjectActions();
  project_actions_btn_->Show(open && !is_proj);
  project_actions_lbl_->Show(open && is_proj);

  codegen_bar_->Show(open);
  if (open) {
    const auto& d = ia.Desc();
    syncing_ = true;
    gen_cb_->SetValue(!d.header.empty());
    if (!class_edit_->HasFocus()) {
      class_edit_->ChangeValue(wxString::FromUTF8(d.class_name));
    }
    if (!header_edit_->HasFocus()) {
      header_edit_->ChangeValue(wxString::FromUTF8(d.header));
    }
    class_edit_->Enable(!d.header.empty());
    header_edit_->Enable(!d.header.empty());
    write_header_btn_->Enable(!d.header.empty());
    syncing_ = false;

    if (!ia.HeaderErrors().empty()) {
      header_err_lbl_->SetLabel(wxString::FromUTF8(ia.HeaderErrors().front()));
    } else {
      header_err_lbl_->SetLabel("");
    }
  }
  codegen_bar_->Layout();
  Layout();
}

void InputActionsPanel::RefreshMapsList() {
  auto& ia = scene_.InputActionsRef();
  syncing_ = true;
  maps_list_->Clear();
  for (const auto& m : ia.Desc().maps) {
    maps_list_->Append(wxString::FromUTF8(m));
  }
  if (ia.SelectedMapIndex() >= 0 &&
      ia.SelectedMapIndex() < static_cast<int>(ia.Desc().maps.size())) {
    maps_list_->SetSelection(ia.SelectedMapIndex());
  }
  syncing_ = false;

  const bool has_map = !ia.SelectedMap().empty();
  rename_map_btn_->Enable(has_map);
  del_map_btn_->Enable(has_map);
}

void InputActionsPanel::RefreshActionsTree() {
  auto& ia = scene_.InputActionsRef();
  const auto& desc = ia.Desc();
  const std::string cur_map = ia.SelectedMap();
  const auto cur_sel = ia.CurrentSelection();

  syncing_ = true;
  actions_tree_->DeleteAllItems();
  wxTreeItemId root = actions_tree_->AddRoot("Root");
  wxTreeItemId to_select;

  for (int action_idx : ia.ActionsOf(cur_map)) {
    const auto& act = desc.actions[action_idx];
    InputActionsWindow::Selection a_sel{
        InputActionsWindow::Selection::Kind::kAction, action_idx, -1};
    wxTreeItemId act_id =
        actions_tree_->AppendItem(root, wxString::FromUTF8(ActionSummary(act)),
                                  -1, -1, new SelectionItemData(a_sel));
    actions_tree_->SetItemFont(act_id, actions_tree_->GetFont().Bold());
    if (cur_sel == a_sel) to_select = act_id;

    for (size_t b = 0; b < act.bindings.size(); ++b) {
      InputActionsWindow::Selection b_sel{
          InputActionsWindow::Selection::Kind::kBinding, action_idx,
          static_cast<int>(b)};
      const std::string label =
          "[B] " + (act.bindings[b].empty() ? std::string("(unbound)")
                                            : act.bindings[b]);
      wxTreeItemId b_id =
          actions_tree_->AppendItem(act_id, wxString::FromUTF8(label), -1, -1,
                                    new SelectionItemData(b_sel));
      if (cur_sel == b_sel) to_select = b_id;
    }

    for (size_t c = 0; c < act.composites.size(); ++c) {
      InputActionsWindow::Selection c_sel{
          InputActionsWindow::Selection::Kind::kComposite, action_idx,
          static_cast<int>(c)};
      wxTreeItemId c_id = actions_tree_->AppendItem(
          act_id,
          wxString::FromUTF8("[2D] " + CompositeSummary(act.composites[c])), -1,
          -1, new SelectionItemData(c_sel));
      if (cur_sel == c_sel) to_select = c_id;
    }
    actions_tree_->Expand(act_id);
  }

  if (to_select.IsOk()) actions_tree_->SelectItem(to_select);
  syncing_ = false;

  const bool has_action = cur_sel.action >= 0;
  add_action_btn_->Enable(!cur_map.empty());
  add_binding_btn_->Enable(has_action);
  add_comp_btn_->Enable(has_action);
  dup_btn_->Enable(cur_sel.kind != InputActionsWindow::Selection::Kind::kNone);
  del_btn_->Enable(cur_sel.kind != InputActionsWindow::Selection::Kind::kNone);
}

void InputActionsPanel::SchedulePropsRefresh() {
  if (props_refresh_pending_) return;
  props_refresh_pending_ = true;
  CallAfter([this]() {
    props_refresh_pending_ = false;
    RefreshProperties();
  });
}

void InputActionsPanel::ShowControlPickerMenu(
    wxWindow* anchor, std::function<void(const std::string&)> on_pick) {
  auto paths = KnownControlPaths();
  std::map<std::string, std::vector<std::string>> groups;
  for (std::string_view sv : paths) {
    std::string p(sv);
    std::string cat = "Other";
    auto close = p.find('>');
    if (p.starts_with('<') && close != std::string::npos) {
      cat = p.substr(1, close - 1);
    }
    groups[cat].push_back(std::move(p));
  }

  wxMenu menu;
  std::vector<std::string> id_to_path;
  constexpr int kSearchId = 1;
  menu.Append(kSearchId, "Search Control Path...");
  menu.AppendSeparator();

  int next_id = 100;
  for (const auto& [cat, list] : groups) {
    wxMenu* sub = new wxMenu();
    for (const auto& path : list) {
      id_to_path.push_back(path);
      sub->Append(next_id++, wxString::FromUTF8(path));
    }
    menu.AppendSubMenu(sub, wxString::FromUTF8(cat));
  }

  menu.Bind(wxEVT_MENU, [&](wxCommandEvent& evt) {
    int id = evt.GetId();
    if (id == kSearchId) {
      auto query = PromptTextDialog(
          this, "Search Control Path",
          "Enter control path or substring (e.g. <Keyboard>/space):");
      if (query.has_value() && !query->empty()) {
        std::string lower = *query;
        for (char& c : lower) {
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        std::string matched = *query;
        for (std::string_view sv : paths) {
          std::string cand(sv);
          std::string cl = cand;
          for (char& c : cl) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          }
          if (cl == lower) {
            matched = cand;
            break;
          }
          if (cl.find(lower) != std::string::npos && matched == *query) {
            matched = cand;
          }
        }
        on_pick(matched);
      }
      return;
    }
    int idx = id - 100;
    if (idx >= 0 && static_cast<size_t>(idx) < id_to_path.size()) {
      on_pick(id_to_path[idx]);
    }
  });
  anchor->PopupMenu(&menu);
}

void InputActionsPanel::StartListen(ListenTarget target) {
  listen_ = target;
  for (bool& b : listen_pad_) b = false;
  SetFocus();
  SchedulePropsRefresh();
}

void InputActionsPanel::StopListen() {
  if (listen_.kind == ListenTarget::Kind::kNone) return;
  listen_.kind = ListenTarget::Kind::kNone;
  SchedulePropsRefresh();
}

void InputActionsPanel::ApplyListenPath(const std::string& control_path) {
  auto& ia = scene_.InputActionsRef();
  if (listen_.kind == ListenTarget::Kind::kBinding) {
    ia.SetBindingPath(listen_.action, listen_.index, control_path, false);
  } else if (listen_.kind == ListenTarget::Kind::kCompositePart) {
    ia.SetCompositePartPath(listen_.action, listen_.index, listen_.part,
                            listen_.part_idx, control_path, false);
  }
  listen_.kind = ListenTarget::Kind::kNone;
  RefreshAll(true);
}

void InputActionsPanel::OnCharHook(wxKeyEvent& evt) {
  if (listen_.kind == ListenTarget::Kind::kNone) {
    evt.Skip();
    return;
  }
  const int kc = evt.GetKeyCode();
  if (kc == WXK_ESCAPE) {
    StopListen();
    return;
  }
  int glfw_key = WxKeyToGlfwKey(kc);
  if (glfw_key >= 0) {
    std::string ctrl = ControlFromKey(glfw_key);
    if (!ctrl.empty()) {
      ApplyListenPath("<Keyboard>/" + ctrl);
      return;
    }
  }
  evt.Skip();
}

void InputActionsPanel::RefreshProperties() {
  editing_prop_text_ = false;
  dup_name_lbl_ = nullptr;
  props_scroll_->Freeze();
  props_sizer_->Clear(true);

  auto& ia = scene_.InputActionsRef();
  const auto sel = ia.CurrentSelection();
  last_sel_ = sel;

  if (!ia.IsOpen() || sel.kind == InputActionsWindow::Selection::Kind::kNone ||
      sel.action < 0 ||
      static_cast<size_t>(sel.action) >= ia.Desc().actions.size()) {
    auto* hint = new wxStaticText(
        props_scroll_, wxID_ANY,
        "Select an action, binding, or 2D vector composite to inspect.");
    hint->SetForegroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    props_sizer_->Add(hint, 0, wxALL, 4);
    props_scroll_->Thaw();
    props_scroll_->Layout();
    props_scroll_->FitInside();
    return;
  }

  const auto& act = ia.Desc().actions[sel.action];

  if (sel.kind == InputActionsWindow::Selection::Kind::kAction) {
    auto* name_row = new wxBoxSizer(wxHORIZONTAL);
    auto* nl = new wxStaticText(props_scroll_, wxID_ANY,
                                "Name:", wxDefaultPosition, wxSize(95, -1));
    auto* name_ctrl =
        new wxTextCtrl(props_scroll_, wxID_ANY, wxString::FromUTF8(act.name),
                       wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    name_row->Add(nl, 0, wxALIGN_CENTER_VERTICAL);
    name_row->Add(name_ctrl, 1, wxEXPAND);
    props_sizer_->Add(name_row, 0, wxEXPAND | wxBOTTOM, 4);

    dup_name_lbl_ = new wxStaticText(props_scroll_, wxID_ANY,
                                     "Action name already exists in this map.");
    dup_name_lbl_->SetFont(props_scroll_->GetFont().Bold());
    dup_name_lbl_->Show(false);
    props_sizer_->Add(dup_name_lbl_, 0, wxBOTTOM, 6);

    const int aidx = sel.action;
    name_ctrl->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
      editing_prop_text_ = true;
      scene_.InputActionsRef().BeginEdit();
      evt.Skip();
    });
    name_ctrl->Bind(wxEVT_TEXT, [this, name_ctrl, aidx](wxCommandEvent&) {
      const std::string v = name_ctrl->GetValue().ToStdString();
      scene_.InputActionsRef().SetActionName(aidx, v, false);
      if (dup_name_lbl_) {
        dup_name_lbl_->Show(
            scene_.InputActionsRef().IsActionNameTaken(aidx, v));
        props_scroll_->Layout();
      }
      RefreshActionsTree();
    });
    auto finish_name = [this, name_ctrl, aidx]() {
      editing_prop_text_ = false;
      std::string committed = scene_.InputActionsRef().SetActionName(
          aidx, name_ctrl->GetValue().ToStdString(), true);
      scene_.InputActionsRef().CommitEdit();
      RefreshAll(true);
    };
    name_ctrl->Bind(wxEVT_TEXT_ENTER,
                    [finish_name](wxCommandEvent&) { finish_name(); });
    name_ctrl->Bind(wxEVT_KILL_FOCUS, [finish_name](wxFocusEvent& evt) {
      finish_name();
      evt.Skip();
    });

    // Action Type
    auto* type_row = new wxBoxSizer(wxHORIZONTAL);
    auto* tl =
        new wxStaticText(props_scroll_, wxID_ANY,
                         "Action Type:", wxDefaultPosition, wxSize(95, -1));
    auto* type_choice = new wxChoice(props_scroll_, wxID_ANY);
    type_choice->Append("Button");
    type_choice->Append("Value");
    type_choice->Append("PassThrough");
    type_choice->SetSelection(static_cast<int>(act.type));
    type_choice->Bind(wxEVT_CHOICE, [this, aidx](wxCommandEvent& evt) {
      scene_.InputActionsRef().SetActionType(
          aidx, static_cast<InputAction::Type>(evt.GetSelection()));
      RefreshAll(true);
    });
    type_row->Add(tl, 0, wxALIGN_CENTER_VERTICAL);
    type_row->Add(type_choice, 1, wxEXPAND);
    props_sizer_->Add(type_row, 0, wxEXPAND | wxBOTTOM, 6);

    // Control Type
    auto* ctrl_row = new wxBoxSizer(wxHORIZONTAL);
    auto* cl =
        new wxStaticText(props_scroll_, wxID_ANY,
                         "Control Type:", wxDefaultPosition, wxSize(95, -1));
    auto* ctrl_choice = new wxChoice(props_scroll_, wxID_ANY);
    ctrl_choice->Append("Axis / Button");
    ctrl_choice->Append("Vector2");
    ctrl_choice->SetSelection(act.vector2 ? 1 : 0);
    ctrl_choice->Bind(wxEVT_CHOICE, [this, aidx](wxCommandEvent& evt) {
      scene_.InputActionsRef().SetActionVector2(aidx, evt.GetSelection() == 1);
      RefreshAll(true);
    });
    ctrl_row->Add(cl, 0, wxALIGN_CENTER_VERTICAL);
    ctrl_row->Add(ctrl_choice, 1, wxEXPAND);
    props_sizer_->Add(ctrl_row, 0, wxEXPAND | wxBOTTOM, 10);

    // Quick add buttons
    auto* qbtns = new wxBoxSizer(wxHORIZONTAL);
    auto* ab = new wxButton(props_scroll_, wxID_ANY, "Add Binding");
    auto* ac = new wxButton(props_scroll_, wxID_ANY, "Add 2D Vector Composite");
    ab->Bind(wxEVT_BUTTON, [this, aidx](wxCommandEvent&) {
      scene_.InputActionsRef().AddBinding(aidx);
      RefreshAll(true);
    });
    ac->Bind(wxEVT_BUTTON, [this, aidx](wxCommandEvent&) {
      scene_.InputActionsRef().AddComposite(aidx);
      RefreshAll(true);
    });
    qbtns->Add(ab, 0, wxRIGHT, 6);
    qbtns->Add(ac, 0);
    props_sizer_->Add(qbtns, 0, wxBOTTOM, 12);

    // AngelScript Usage Snippet
    const std::string snippet = ia.ActionUsageSnippet(aidx);
    if (!snippet.empty()) {
      auto* ul =
          new wxStaticText(props_scroll_, wxID_ANY, "AngelScript Usage:");
      props_sizer_->Add(ul, 0, wxBOTTOM, 4);

      auto* code =
          new wxTextCtrl(props_scroll_, wxID_ANY, wxString::FromUTF8(snippet),
                         wxDefaultPosition, wxSize(-1, 68),
                         wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
      code->SetFont(MonospaceFont(9));
      props_sizer_->Add(code, 0, wxEXPAND | wxBOTTOM, 4);

      auto* copy_btn = new wxButton(props_scroll_, wxID_ANY, "Copy Snippet");
      ApplyButtonPadding(copy_btn, 10);
      copy_btn->Bind(wxEVT_BUTTON, [snippet](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
          wxTheClipboard->SetData(
              new wxTextDataObject(wxString::FromUTF8(snippet)));
          wxTheClipboard->Close();
        }
      });
      props_sizer_->Add(copy_btn, 0);
    }
  } else if (sel.kind == InputActionsWindow::Selection::Kind::kBinding) {
    if (sel.index >= 0 &&
        static_cast<size_t>(sel.index) < act.bindings.size()) {
      const int aidx = sel.action;
      const int bidx = sel.index;
      const std::string& path = act.bindings[bidx];

      auto* title =
          new wxStaticText(props_scroll_, wxID_ANY,
                           wxString::FromUTF8("Binding on " + act.FullName()));
      props_sizer_->Add(title, 0, wxBOTTOM, 8);

      auto* row = new wxBoxSizer(wxHORIZONTAL);
      auto* edit =
          new wxTextCtrl(props_scroll_, wxID_ANY, wxString::FromUTF8(path),
                         wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
      edit->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
        editing_prop_text_ = true;
        scene_.InputActionsRef().BeginEdit();
        evt.Skip();
      });
      edit->Bind(wxEVT_TEXT, [this, edit, aidx, bidx](wxCommandEvent&) {
        scene_.InputActionsRef().SetBindingPath(
            aidx, bidx, edit->GetValue().ToStdString(), true);
        RefreshActionsTree();
      });
      auto commit = [this]() {
        editing_prop_text_ = false;
        scene_.InputActionsRef().CommitEdit();
      };
      edit->Bind(wxEVT_TEXT_ENTER, [commit, this](wxCommandEvent&) {
        commit();
        RefreshAll(true);
      });
      edit->Bind(wxEVT_KILL_FOCUS, [commit](wxFocusEvent& evt) {
        commit();
        evt.Skip();
      });

      auto* pick_btn = new wxButton(props_scroll_, wxID_ANY, "Pick...");
      ApplyButtonPadding(pick_btn, 8);
      pick_btn->Bind(
          wxEVT_BUTTON, [this, pick_btn, aidx, bidx](wxCommandEvent&) {
            ShowControlPickerMenu(
                pick_btn, [this, aidx, bidx](const std::string& p) {
                  scene_.InputActionsRef().SetBindingPath(aidx, bidx, p, false);
                  RefreshAll(true);
                });
          });

      const bool listening = listen_.kind == ListenTarget::Kind::kBinding &&
                             listen_.action == aidx && listen_.index == bidx;
      auto* listen_btn = new wxButton(props_scroll_, wxID_ANY,
                                      listening ? "Listening..." : "Listen");
      ApplyButtonPadding(listen_btn, 8);
      listen_btn->Bind(
          wxEVT_BUTTON, [this, listening, aidx, bidx](wxCommandEvent&) {
            if (listening) {
              StopListen();
            } else {
              StartListen({ListenTarget::Kind::kBinding, aidx, bidx, -1, -1});
            }
          });

      row->Add(edit, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      row->Add(pick_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      row->Add(listen_btn, 0, wxALIGN_CENTER_VERTICAL);
      props_sizer_->Add(row, 0, wxEXPAND | wxBOTTOM, 8);

      if (listening) {
        auto* lhint = new wxStaticText(props_scroll_, wxID_ANY,
                                       "Press a keyboard key or gamepad "
                                       "button/trigger (Esc to cancel)...");
        lhint->SetFont(props_scroll_->GetFont().Bold());
        props_sizer_->Add(lhint, 0);
      }
    }
  } else if (sel.kind == InputActionsWindow::Selection::Kind::kComposite) {
    if (sel.index >= 0 &&
        static_cast<size_t>(sel.index) < act.composites.size()) {
      const int aidx = sel.action;
      const int cidx = sel.index;
      const auto& comp = act.composites[cidx];

      auto* title = new wxStaticText(
          props_scroll_, wxID_ANY,
          wxString::FromUTF8("2D Vector Composite on " + act.FullName()));
      props_sizer_->Add(title, 0, wxBOTTOM, 8);

      const char* part_names[4] = {"Up", "Down", "Left", "Right"};
      const std::vector<std::string>* parts[4] = {&comp.up, &comp.down,
                                                  &comp.left, &comp.right};

      for (int p = 0; p < 4; ++p) {
        auto* p_hdr = new wxBoxSizer(wxHORIZONTAL);
        auto* pl = new wxStaticText(props_scroll_, wxID_ANY, part_names[p]);
        pl->SetFont(HeaderFont());
        auto* add_p = new wxButton(props_scroll_, wxID_ANY, "Add");
        ApplyButtonIconAndPadding(add_p, EditorIcon::kCreate, 8, 13, 5);
        add_p->Bind(wxEVT_BUTTON, [this, aidx, cidx, p](wxCommandEvent&) {
          scene_.InputActionsRef().AddCompositePartPath(aidx, cidx, p);
          RefreshAll(true);
        });
        p_hdr->Add(pl, 0, wxALIGN_CENTER_VERTICAL);
        p_hdr->AddStretchSpacer();
        p_hdr->Add(add_p, 0);
        props_sizer_->Add(p_hdr, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);

        const auto& vec = *parts[p];
        for (int idx = 0; idx < static_cast<int>(vec.size()); ++idx) {
          auto* row = new wxBoxSizer(wxHORIZONTAL);
          auto* edit = new wxTextCtrl(
              props_scroll_, wxID_ANY, wxString::FromUTF8(vec[idx]),
              wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
          edit->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& evt) {
            editing_prop_text_ = true;
            scene_.InputActionsRef().BeginEdit();
            evt.Skip();
          });
          edit->Bind(
              wxEVT_TEXT, [this, edit, aidx, cidx, p, idx](wxCommandEvent&) {
                scene_.InputActionsRef().SetCompositePartPath(
                    aidx, cidx, p, idx, edit->GetValue().ToStdString(), true);
                RefreshActionsTree();
              });
          edit->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& evt) {
            editing_prop_text_ = false;
            scene_.InputActionsRef().CommitEdit();
            evt.Skip();
          });

          auto* pick_btn = new wxButton(props_scroll_, wxID_ANY, "Pick...");
          ApplyButtonPadding(pick_btn, 8);
          pick_btn->Bind(wxEVT_BUTTON, [this, pick_btn, aidx, cidx, p,
                                        idx](wxCommandEvent&) {
            ShowControlPickerMenu(
                pick_btn, [this, aidx, cidx, p, idx](const std::string& path) {
                  scene_.InputActionsRef().SetCompositePartPath(
                      aidx, cidx, p, idx, path, false);
                  RefreshAll(true);
                });
          });

          const bool listening =
              listen_.kind == ListenTarget::Kind::kCompositePart &&
              listen_.action == aidx && listen_.index == cidx &&
              listen_.part == p && listen_.part_idx == idx;
          auto* listen_btn = new wxButton(props_scroll_, wxID_ANY,
                                          listening ? "..." : "Listen");
          ApplyButtonPadding(listen_btn, 8);
          listen_btn->Bind(wxEVT_BUTTON, [this, listening, aidx, cidx, p,
                                          idx](wxCommandEvent&) {
            if (listening) {
              StopListen();
            } else {
              StartListen(
                  {ListenTarget::Kind::kCompositePart, aidx, cidx, p, idx});
            }
          });

          auto* rem_btn = new wxButton(props_scroll_, wxID_ANY, "-",
                                       wxDefaultPosition, wxSize(26, 22));
          rem_btn->Bind(wxEVT_BUTTON,
                        [this, aidx, cidx, p, idx](wxCommandEvent&) {
                          scene_.InputActionsRef().RemoveCompositePartPath(
                              aidx, cidx, p, idx);
                          RefreshAll(true);
                        });

          row->Add(edit, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
          row->Add(pick_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
          row->Add(listen_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
          row->Add(rem_btn, 0, wxALIGN_CENTER_VERTICAL);
          props_sizer_->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
        }
      }
    }
  }

  props_scroll_->Thaw();
  props_scroll_->Layout();
  props_scroll_->FitInside();
}

void InputActionsPanel::RefreshAll(bool rebuild_props) {
  auto& ia = scene_.InputActionsRef();
  last_rev_ = ia.StateRevision();
  last_open_ = ia.IsOpen();

  RefreshFileChoices();
  RefreshTopBar();

  const bool open = ia.IsOpen();
  empty_banner_->Show(!open);
  left_split_->Show(open);

  if (open) {
    RefreshMapsList();
    RefreshActionsTree();
    if (rebuild_props) RefreshProperties();
  }
  Layout();
}

void InputActionsPanel::SetPoppedOut(bool popped_out) {
  const bool changed = (popped_out_ != popped_out);
  popped_out_ = popped_out;
  if (popout_btn_) {
    popout_btn_->SetLabel(popped_out_ ? "Dock" : "Pop Out");
    ApplyButtonIconAndPadding(
        popout_btn_, popped_out_ ? EditorIcon::kDock : EditorIcon::kPopOut);
    popout_btn_->Show(true);
  }
  if (changed) Layout();
}

void InputActionsPanel::SyncFromController(float /*dt*/) {
  auto& ia = scene_.InputActionsRef();

  // Gamepad polling during Listen mode
  if (listen_.kind != ListenTarget::Kind::kNone) {
    if (auto path = InputActionsWindow::PollGamepadListen(
            scene_.GetInputManager(), listen_pad_)) {
      ApplyListenPath(*path);
      return;
    }
  }

  const uint64_t rev = ia.StateRevision();
  const bool open = ia.IsOpen();
  if (rev != last_rev_ || open != last_open_) {
    const bool sel_changed = !(ia.CurrentSelection() == last_sel_);
    RefreshAll(!editing_prop_text_ || sel_changed);
  }
}

// ---------------------------------------------------------------------------
// InputActionsFrame
// ---------------------------------------------------------------------------

InputActionsFrame::InputActionsFrame(wxWindow* parent, EditorScene& scene,
                                     std::function<void()> on_closed)
    : wxFrame(parent, wxID_ANY, "Input Actions", wxDefaultPosition,
              wxSize(980, 620)),
      on_closed_(std::move(on_closed)) {
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  panel_ = new InputActionsPanel(this, scene);
  panel_->SetPoppedOut(true);
  panel_->SetOnPopout([this](bool want_pop) {
    if (!want_pop) Close();
  });
  sizer->Add(panel_, 1, wxEXPAND);
  SetSizer(sizer);

  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) {
    if (auto cb = std::move(on_closed_)) {
      on_closed_ = nullptr;
      cb();
    }
    evt.Skip();
  });
}

void InputActionsFrame::SyncFromController(float dt) {
  panel_->SyncFromController(dt);
}

}  // namespace Mercury::EditorUI
