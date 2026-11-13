#include "ui_builder_panel.h"

#include <wx/dataobj.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/wrapsizer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <sstream>

#include "scene/scene.h"
#include "dialogs.h"
#include "dnd_payloads.h"
#include "project/asset_database.h"
#include "ui/style.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLibPayloadPrefix = "MERCURY_UILIB:";
constexpr const char* kTplPayloadPrefix = "MERCURY_UITPL:";
constexpr const char* kElemPayloadPrefix = "MERCURY_UIELEM:";

struct CanvasPreset {
  const char* label;
  int w;
  int h;
};

constexpr CanvasPreset kPresets[] = {
    {"1920 x 1080", 1920, 1080}, {"1280 x 720", 1280, 720},
    {"2560 x 1440", 2560, 1440}, {"1024 x 768", 1024, 768},
    {"800 x 600", 800, 600},    {"720 x 1280", 720, 1280},
};

std::string EncodePath(const std::vector<int>& path) {
  std::string s;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i > 0) s += ",";
    s += std::to_string(path[i]);
  }
  return s;
}

std::vector<int> DecodePath(const std::string& s) {
  std::vector<int> out;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) {
      try {
        out.push_back(std::stoi(item));
      } catch (...) {
      }
    }
  }
  return out;
}

std::string LowerStr(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

class SheetTreeData : public wxTreeItemData {
 public:
  SheetTreeData(int s, int r) : sheet(s), rule(r) {}
  int sheet;
  int rule;
};

class ElemTreeData : public wxTreeItemData {
 public:
  ElemTreeData(bool doc, std::vector<int> p) : is_doc(doc), path(std::move(p)) {}
  bool is_doc;
  std::vector<int> path;
};

class LibTreeData : public wxTreeItemData {
 public:
  LibTreeData(bool tpl, std::string val)
      : is_template(tpl), payload(std::move(val)) {}
  bool is_template;
  std::string payload;
};

UI::KeyCode WxKeyToUiKey(int key) {
  using K = UI::KeyCode;
  if (key >= 'a' && key <= 'z') key = key - 'a' + 'A';
  switch (key) {
    case WXK_SPACE: return K::kSpace;
    case 'A': return K::kA;
    case 'C': return K::kC;
    case 'V': return K::kV;
    case 'X': return K::kX;
    case 'Y': return K::kY;
    case 'Z': return K::kZ;
    case WXK_ESCAPE: return K::kEscape;
    case WXK_RETURN: return K::kEnter;
    case WXK_NUMPAD_ENTER: return K::kKeypadEnter;
    case WXK_TAB: return K::kTab;
    case WXK_BACK: return K::kBackspace;
    case WXK_INSERT: return K::kInsert;
    case WXK_DELETE: return K::kDelete;
    case WXK_RIGHT: return K::kRight;
    case WXK_LEFT: return K::kLeft;
    case WXK_DOWN: return K::kDown;
    case WXK_UP: return K::kUp;
    case WXK_PAGEUP: return K::kPageUp;
    case WXK_PAGEDOWN: return K::kPageDown;
    case WXK_HOME: return K::kHome;
    case WXK_END: return K::kEnd;
    default: return K::kNone;
  }
}

UI::KeyModifiers WxMods(const wxKeyEvent& evt) {
  UI::KeyModifiers m = UI::kModNone;
  if (evt.ShiftDown()) m |= UI::kModShift;
  if (evt.ControlDown()) m |= UI::kModControl;
  if (evt.AltDown()) m |= UI::kModAlt;
  if (evt.MetaDown()) m |= UI::kModSuper;
  return m;
}

}  // namespace

// --------------------------------------------------------- UIBuilderFrame ---

UIBuilderFrame::UIBuilderFrame(wxWindow* parent, EditorScene& scene,
                               std::function<void()> on_closed)
    : wxFrame(parent, wxID_ANY, "UI Builder", wxDefaultPosition,
              wxSize(1280, 780), wxDEFAULT_FRAME_STYLE),
      on_closed_(std::move(on_closed)) {
  panel_ = new UIBuilderPanel(this, scene);
  panel_->SetPoppedOut(true);
  panel_->SetOnPopout([this](bool want_pop) {
    if (!want_pop) Close();
  });
  auto* sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(panel_, 1, wxEXPAND);
  SetSizer(sizer);

  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& evt) {
    Hide();
    if (on_closed_) on_closed_();
    evt.Veto();
  });
}

void UIBuilderFrame::SyncFromController(float dt) {
  if (!IsShown()) return;
  panel_->SyncFromController(dt);
}

// --------------------------------------------------------- UIBuilderPanel ---

UIBuilderPanel::UIBuilderPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY),
      scene_(scene),
      overlay_font_(wxFontInfo(9).Family(wxFONTFAMILY_SWISS)) {
  auto& builder = scene_.UIBuilderRef();
  builder.list_files = [this](const std::vector<std::string>& exts) {
    return scene_.ListProjectFiles(exts);
  };
  BuildLayout();
  RefreshAll();
}

void UIBuilderPanel::SetPoppedOut(bool popped_out) {
  popped_out_ = popped_out;
  if (popout_btn_) {
    popout_btn_->SetLabel(popped_out_ ? "Dock" : "Pop Out");
  }
}

void UIBuilderPanel::BuildLayout() {
  auto* root = new wxBoxSizer(wxVERTICAL);

  // Top bar
  auto* bar = new wxPanel(this, wxID_ANY);
  auto* bs = new wxBoxSizer(wxHORIZONTAL);

  auto mkbtn = [&](const wxString& txt) {
    return new wxButton(bar, wxID_ANY, txt, wxDefaultPosition, wxDefaultSize,
                        wxBU_EXACTFIT);
  };

  file_lbl_ = new wxStaticText(bar, wxID_ANY, "(no file)", wxDefaultPosition,
                               wxSize(130, -1), wxST_ELLIPSIZE_MIDDLE);
  file_lbl_->SetFont(HeaderFont(0));

  save_btn_ = mkbtn("Save");
  revert_btn_ = mkbtn("Revert");
  close_btn_ = mkbtn("Close");
  undo_btn_ = mkbtn("Undo");
  redo_btn_ = mkbtn("Redo");
  dup_btn_ = mkbtn("Duplicate");
  del_btn_ = mkbtn("Delete");

  preview_cb_ = new wxCheckBox(bar, wxID_ANY, "Preview");

  wxArrayString preset_labels;
  for (const auto& p : kPresets) preset_labels.Add(p.label);
  preset_choice_ = new wxChoice(bar, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                preset_labels);

  canvas_w_ = new DragNumberCtrl(bar, "W", wxNullColour, 1280, 2, true, 0);
  canvas_w_->SetMinSize(wxSize(76, 22));
  canvas_h_ = new DragNumberCtrl(bar, "H", wxNullColour, 720, 2, true, 0);
  canvas_h_->SetMinSize(wxSize(76, 22));

  bg_swatch_ = new ColorSwatchCtrl(bar, scene_.UIBuilderRef().Background());
  bg_swatch_->SetMinSize(wxSize(42, 22));
  bg_swatch_->SetToolTip("Canvas Backdrop Color");

  theme_choice_ = new wxChoice(bar, wxID_ANY);
  theme_choice_->SetMinSize(wxSize(110, -1));

  auto* fit_btn = mkbtn("Fit");
  auto* one_btn = mkbtn("1:1");
  popout_btn_ = mkbtn("Pop Out");

  bs->Add(file_lbl_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bs->Add(save_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(revert_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(close_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  bs->Add(undo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(redo_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(dup_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(del_btn_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  bs->Add(preview_cb_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  bs->Add(preset_choice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  bs->Add(canvas_w_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(canvas_h_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bs->Add(bg_swatch_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bs->Add(theme_choice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bs->Add(fit_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
  bs->Add(one_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  bs->AddStretchSpacer();
  bs->Add(popout_btn_, 0, wxALIGN_CENTER_VERTICAL);

  auto* bar_wrap = new wxBoxSizer(wxVERTICAL);
  bar_wrap->Add(bs, 0, wxEXPAND | wxALL, 4);
  bar->SetSizer(bar_wrap);
  root->Add(bar, 0, wxEXPAND);

  empty_banner_ = new wxPanel(this, wxID_ANY);
  auto* ebs = new wxBoxSizer(wxVERTICAL);
  auto* em = new wxStaticText(
      empty_banner_, wxID_ANY,
      "Open a .uxml file: double-click it in the Project browser, or select a "
      "UIDocument component and click \"Edit in UI Builder\".");
  em->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  ebs->Add(em, 0, wxALL, 20);
  empty_banner_->SetSizer(ebs);
  root->Add(empty_banner_, 1, wxEXPAND);

  // Main 3-way horizontal splitter
  main_split_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition,
                                     wxDefaultSize, wxSP_LIVE_UPDATE);
  main_split_->SetMinimumPaneSize(180);

  // Left pane: StyleSheets | Hierarchy | Library
  auto* left_pane = new wxPanel(main_split_, wxID_ANY);
  auto* left_split1 = new wxSplitterWindow(left_pane, wxID_ANY,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE);
  left_split1->SetMinimumPaneSize(100);

  // Top-left: StyleSheets
  auto* sheets_panel = new wxPanel(left_split1, wxID_ANY);
  auto* shs = new wxBoxSizer(wxVERTICAL);

  auto* sh_hdr = new wxBoxSizer(wxHORIZONTAL);
  auto* sh_lbl = new wxStaticText(sheets_panel, wxID_ANY, "StyleSheets");
  auto* add_sheet_btn =
      new wxButton(sheets_panel, wxID_ANY, "+ Sheet", wxDefaultPosition,
                   wxDefaultSize, wxBU_EXACTFIT);
  sh_hdr->Add(sh_lbl, 1, wxALIGN_CENTER_VERTICAL);
  sh_hdr->Add(add_sheet_btn, 0);
  shs->Add(sh_hdr, 0, wxEXPAND | wxALL, 4);

  auto* sel_row = new wxBoxSizer(wxHORIZONTAL);
  new_selector_ = new wxTextCtrl(sheets_panel, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxDefaultSize,
                                 wxTE_PROCESS_ENTER);
  new_selector_->SetHint(".button or #title");
  auto* add_rule_btn =
      new wxButton(sheets_panel, wxID_ANY, "+", wxDefaultPosition,
                   wxSize(24, 22), wxBU_EXACTFIT);
  sel_row->Add(new_selector_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  sel_row->Add(add_rule_btn, 0, wxALIGN_CENTER_VERTICAL);
  shs->Add(sel_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);

  sheets_tree_ = new wxTreeCtrl(
      sheets_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE);
  shs->Add(sheets_tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  sheets_panel->SetSizer(shs);

  // Lower-left splitter: Hierarchy + Library
  auto* left_split2 = new wxSplitterWindow(left_split1, wxID_ANY,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE);
  left_split2->SetMinimumPaneSize(100);

  auto* hier_panel = new wxPanel(left_split2, wxID_ANY);
  auto* hs = new wxBoxSizer(wxVERTICAL);
  auto* hl = new wxStaticText(hier_panel, wxID_ANY, "Hierarchy");
  hs->Add(hl, 0, wxALL, 4);
  hier_tree_ = new wxTreeCtrl(
      hier_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE);
  hs->Add(hier_tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  hier_panel->SetSizer(hs);

  auto* lib_panel = new wxPanel(left_split2, wxID_ANY);
  auto* lbs = new wxBoxSizer(wxVERTICAL);
  auto* ll = new wxStaticText(lib_panel, wxID_ANY, "Library");
  lbs->Add(ll, 0, wxALL, 4);
  lib_search_ = new wxTextCtrl(lib_panel, wxID_ANY);
  lib_search_->SetHint("Search controls...");
  lbs->Add(lib_search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  lib_tree_ = new wxTreeCtrl(
      lib_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxTR_HAS_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE);
  lbs->Add(lib_tree_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 4);
  lib_panel->SetSizer(lbs);

  left_split2->SplitHorizontally(hier_panel, lib_panel, 210);
  left_split1->SplitHorizontally(sheets_panel, left_split2, 160);
  auto* lwrap = new wxBoxSizer(wxVERTICAL);
  lwrap->Add(left_split1, 1, wxEXPAND);
  left_pane->SetSizer(lwrap);

  // Right splitter: [Center Canvas/Code] | [Inspector]
  auto* right_split = new wxSplitterWindow(main_split_, wxID_ANY,
                                           wxDefaultPosition, wxDefaultSize,
                                           wxSP_LIVE_UPDATE);
  right_split->SetMinimumPaneSize(220);

  auto* center_split = new wxSplitterWindow(right_split, wxID_ANY,
                                            wxDefaultPosition, wxDefaultSize,
                                            wxSP_LIVE_UPDATE);
  center_split->SetMinimumPaneSize(120);

  canvas_ = new wxPanel(center_split, wxID_ANY, wxDefaultPosition,
                        wxDefaultSize,
                        wxBORDER_NONE | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE);
  canvas_->SetBackgroundStyle(wxBG_STYLE_PAINT);
  canvas_->SetBackgroundColour(wxColour(24, 26, 30));

  code_tabs_ = new wxNotebook(center_split, wxID_ANY);

  center_split->SplitHorizontally(canvas_, code_tabs_, 460);

  inspector_scroll_ = new wxScrolledWindow(
      right_split, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxVSCROLL | wxBORDER_NONE);
  inspector_scroll_->SetScrollRate(0, 14);
  inspector_sizer_ = new wxBoxSizer(wxVERTICAL);
  inspector_scroll_->SetSizer(inspector_sizer_);

  right_split->SplitVertically(center_split, inspector_scroll_, -310);
  right_split->SetSashGravity(1.0);
  main_split_->SplitVertically(left_pane, right_split, 250);

  root->Add(main_split_, 1, wxEXPAND);
  SetSizer(root);

  // --- Top bar event wiring ---
  save_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().Save();
  });
  revert_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto& b = scene_.UIBuilderRef();
    if (!b.path().empty()) b.Open(b.path());
  });
  close_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    auto& b = scene_.UIBuilderRef();
    if (b.IsDirty()) {
      auto choice = ShowUnsavedChangesDialog(
          this, "Save changes to the UI document before closing?");
      if (choice == UnsavedChoice::kCancel) return;
      if (choice == UnsavedChoice::kSave && !b.Save()) return;
    }
    b.Close();
  });
  undo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().Undo();
  });
  redo_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().Redo();
  });
  dup_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().DuplicateSelected();
  });
  del_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().DeleteSelected();
  });
  preview_cb_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().SetPreviewMode(preview_cb_->GetValue());
  });
  preset_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    const int idx = preset_choice_->GetSelection();
    if (idx >= 0 && idx < static_cast<int>(std::size(kPresets))) {
      scene_.UIBuilderRef().SetCanvasSize({kPresets[idx].w, kPresets[idx].h},
                                          true);
    }
  });
  canvas_w_->SetCallbacks(
      nullptr,
      [this](double v) {
        auto sz = scene_.UIBuilderRef().CanvasSize();
        sz.x = std::max(32, static_cast<int>(std::llround(v)));
        scene_.UIBuilderRef().SetCanvasSize(sz);
      },
      nullptr);
  canvas_h_->SetCallbacks(
      nullptr,
      [this](double v) {
        auto sz = scene_.UIBuilderRef().CanvasSize();
        sz.y = std::max(32, static_cast<int>(std::llround(v)));
        scene_.UIBuilderRef().SetCanvasSize(sz);
      },
      nullptr);
  bg_swatch_->SetOnChange([this](glm::vec4 c) {
    scene_.UIBuilderRef().SetBackground(c);
  });
  theme_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
    const int idx = theme_choice_->GetSelection();
    if (idx >= 0 && idx < static_cast<int>(theme_paths_.size())) {
      scene_.UIBuilderRef().SetTheme(theme_paths_[idx]);
    }
  });
  fit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const wxSize sz = canvas_->GetClientSize();
    scene_.UIBuilderRef().FitToViewport(
        {static_cast<float>(sz.x), static_cast<float>(sz.y)});
  });
  one_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.UIBuilderRef().ResetZoom1To1();
  });
  popout_btn_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    if (on_popout_) on_popout_(!popped_out_);
  });

  // --- StyleSheets tree events ---
  auto add_rule_fn = [this]() {
    std::string err;
    const std::string sel = new_selector_->GetValue().ToStdString();
    if (sel.empty()) return;
    if (scene_.UIBuilderRef().AddRuleToActiveSheet(sel, &err)) {
      new_selector_->Clear();
    } else if (!err.empty()) {
      scene_.ConsoleRef().Error("Selector: " + err);
    }
  };
  new_selector_->Bind(wxEVT_TEXT_ENTER, [add_rule_fn](wxCommandEvent&) {
    add_rule_fn();
  });
  add_rule_btn->Bind(wxEVT_BUTTON, [add_rule_fn](wxCommandEvent&) {
    add_rule_fn();
  });

  add_sheet_btn->Bind(wxEVT_BUTTON, [this, add_sheet_btn](wxCommandEvent&) {
    wxMenu menu;
    const auto uss_files =
        scene_.ListProjectFiles({std::string(kUssExtension)});
    constexpr int kNewId = 9000;
    menu.Append(kNewId, "New StyleSheet Next to Document...");
    menu.AppendSeparator();
    for (size_t i = 0; i < uss_files.size(); ++i) {
      menu.Append(static_cast<int>(i + 1), wxString::FromUTF8(uss_files[i]));
    }
    menu.Bind(wxEVT_MENU, [this, uss_files](wxCommandEvent& evt) {
      auto& b = scene_.UIBuilderRef();
      if (evt.GetId() == 9000) {
        b.CreateNewSheetNextToDoc();
      } else {
        const size_t idx = static_cast<size_t>(evt.GetId() - 1);
        if (idx < uss_files.size()) {
          b.AddSheet(b.RelativeToDocument(uss_files[idx]));
        }
      }
    });
    add_sheet_btn->PopupMenu(&menu, 0, add_sheet_btn->GetSize().y);
  });

  sheets_tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& evt) {
    if (syncing_trees_) return;
    auto* data = static_cast<SheetTreeData*>(
        sheets_tree_->GetItemData(evt.GetItem()));
    if (!data) return;
    auto& b = scene_.UIBuilderRef();
    b.SetActiveSheet(data->sheet);
    if (data->rule >= 0) {
      b.Select({UIBuilder::Selection::Kind::kSelector, {}, data->sheet,
                data->rule});
    }
  });

  sheets_tree_->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, [this](wxTreeEvent& evt) {
    const wxTreeItemId item = evt.GetItem();
    if (!item.IsOk()) return;
    sheets_tree_->SelectItem(item);
    auto* data = static_cast<SheetTreeData*>(sheets_tree_->GetItemData(item));
    if (!data) return;
    wxMenu menu;
    if (data->rule < 0) {
      menu.Append(1, "Remove StyleSheet from Document");
      menu.Bind(wxEVT_MENU, [this, sheet = data->sheet](wxCommandEvent&) {
        scene_.UIBuilderRef().RemoveSheet(sheet);
      });
    } else {
      menu.Append(1, "Add Variable...");
      menu.Append(2, "Duplicate Rule");
      menu.Append(3, "Rename Rule...");
      menu.AppendSeparator();
      menu.Append(4, "Delete Rule");
      const int sh = data->sheet;
      const int rl = data->rule;
      menu.Bind(wxEVT_MENU, [this, sh, rl](wxCommandEvent& cmd) {
        auto& ub = scene_.UIBuilderRef();
        if (sh < 0 || sh >= static_cast<int>(ub.Sheets().size())) return;
        auto& sheet_entry = ub.Sheets()[sh];
        if (!sheet_entry.sheet || rl < 0 ||
            rl >= static_cast<int>(sheet_entry.sheet->rules.size())) {
          return;
        }
        auto* rule = &sheet_entry.sheet->rules[rl];
        if (cmd.GetId() == 1) {
          if (auto vname =
                  PromptTextDialog(this, "Add Variable", "Name (--my-var)",
                                   "--", "Add")) {
            ub.AddVariable(rule, sh, *vname);
          }
        } else if (cmd.GetId() == 2) {
          ub.DuplicateRule(sh, rl);
        } else if (cmd.GetId() == 3) {
          if (auto rname = PromptTextDialog(
                  this, "Rename Selector", "Selector",
                  wxString::FromUTF8(rule->SelectorText()), "Rename")) {
            ub.RenameRule(rule, sh, *rname);
          }
        } else if (cmd.GetId() == 4) {
          ub.DeleteRule(sh, rl);
        }
      });
    }
    PopupMenu(&menu);
  });

  // --- Hierarchy tree events ---
  hier_tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent& evt) {
    if (syncing_trees_) return;
    auto* data =
        static_cast<ElemTreeData*>(hier_tree_->GetItemData(evt.GetItem()));
    if (!data) return;
    if (data->is_doc) {
      scene_.UIBuilderRef().Select(
          {UIBuilder::Selection::Kind::kDocument, {}, -1, -1});
    } else {
      scene_.UIBuilderRef().Select(
          {UIBuilder::Selection::Kind::kElement, data->path, -1, -1});
    }
  });

  hier_tree_->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& evt) {
    auto* data =
        static_cast<ElemTreeData*>(hier_tree_->GetItemData(evt.GetItem()));
    if (!data || data->is_doc) return;
    wxTextDataObject payload(wxString::FromUTF8(
        std::string(kElemPayloadPrefix) + EncodePath(data->path)));
    wxDropSource src(hier_tree_);
    src.SetData(payload);
    src.DoDragDrop(wxDrag_AllowMove);
  });

  hier_tree_->SetDropTarget(new CompositeDropTarget(
      nullptr,
      [this](wxCoord x, wxCoord y, const wxString& text) -> bool {
        int flags = 0;
        const wxTreeItemId hit = hier_tree_->HitTest(wxPoint(x, y), flags);
        std::vector<int> target_parent;
        if (hit.IsOk()) {
          if (auto* d = static_cast<ElemTreeData*>(hier_tree_->GetItemData(hit))) {
            target_parent = d->path;
          }
        }
        auto& ub = scene_.UIBuilderRef();
        const std::string s = text.ToStdString();
        if (s.rfind(kElemPayloadPrefix, 0) == 0) {
          const auto from = DecodePath(
              s.substr(std::char_traits<char>::length(kElemPayloadPrefix)));
          int idx = -1;
          if (auto* parent = ub.NodeAt(target_parent)) {
            idx = static_cast<int>(parent->children.size());
          } else if (ub.Doc()) {
            idx = static_cast<int>(ub.Doc()->root.children.size());
          }
          ub.MoveNode(from, target_parent, idx);
          return true;
        }
        if (s.rfind(kLibPayloadPrefix, 0) == 0) {
          const std::string t =
              s.substr(std::char_traits<char>::length(kLibPayloadPrefix));
          int idx = ub.Doc() ? static_cast<int>(ub.Doc()->root.children.size()) : 0;
          if (auto* parent = ub.NodeAt(target_parent)) {
            idx = static_cast<int>(parent->children.size());
          }
          ub.InsertNode(target_parent, idx, ub.NewNode(t));
          return true;
        }
        return false;
      }));

  hier_tree_->Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, [this](wxTreeEvent& evt) {
    const wxTreeItemId item = evt.GetItem();
    if (!item.IsOk()) return;
    hier_tree_->SelectItem(item);
    auto* data = static_cast<ElemTreeData*>(hier_tree_->GetItemData(item));
    if (!data) return;
    auto& ub = scene_.UIBuilderRef();
    const bool is_doc = data->is_doc;
    const std::vector<int> path = data->path;

    wxMenu menu;
    auto* add_sub = new wxMenu();
    const auto& types = UI::UxmlFactoryRegistry::Instance().Types();
    for (size_t i = 0; i < types.size(); ++i) {
      add_sub->Append(1000 + static_cast<int>(i),
                      wxString::FromUTF8(types[i]->name));
    }
    menu.AppendSubMenu(add_sub, "Add Child");
    if (!is_doc) {
      menu.Append(1, "Duplicate");
      menu.Append(2, "Move Up");
      menu.Append(3, "Move Down");
      menu.Append(4, "Wrap in VisualElement");
      menu.Append(5, "Unwrap");
      menu.AppendSeparator();
      menu.Append(6, "Delete");
    }
    menu.Bind(wxEVT_MENU, [this, &ub, path, types](wxCommandEvent& cmd) {
      const int id = cmd.GetId();
      if (id >= 1000 && id < 1000 + static_cast<int>(types.size())) {
        const std::string& type_name = types[id - 1000]->name;
        int idx = 0;
        if (path.empty()) {
          idx = ub.Doc() ? static_cast<int>(ub.Doc()->root.children.size()) : 0;
        } else if (auto* p = ub.NodeAt(path)) {
          idx = static_cast<int>(p->children.size());
        }
        ub.InsertNode(path, idx, ub.NewNode(type_name));
        return;
      }
      if (id == 1) ub.DuplicateSelected();
      else if (id == 2 && !path.empty()) {
        std::vector<int> parent(path.begin(), path.end() - 1);
        ub.MoveNode(path, parent, std::max(0, path.back() - 1));
      } else if (id == 3 && !path.empty()) {
        std::vector<int> parent(path.begin(), path.end() - 1);
        ub.MoveNode(path, parent, path.back() + 2);
      } else if (id == 4) {
        ub.WrapInVisualElement(path);
      } else if (id == 5) {
        ub.UnwrapElement(path);
      } else if (id == 6) {
        ub.DeleteSelected();
      }
    });
    PopupMenu(&menu);
  });

  // --- Library tree events ---
  lib_search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { RefreshLibrary(); });

  lib_tree_->Bind(wxEVT_TREE_BEGIN_DRAG, [this](wxTreeEvent& evt) {
    auto* data =
        static_cast<LibTreeData*>(lib_tree_->GetItemData(evt.GetItem()));
    if (!data) return;
    const std::string prefix =
        data->is_template ? kTplPayloadPrefix : kLibPayloadPrefix;
    wxTextDataObject payload(wxString::FromUTF8(prefix + data->payload));
    wxDropSource src(lib_tree_);
    src.SetData(payload);
    src.DoDragDrop(wxDrag_CopyOnly);
  });

  lib_tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& evt) {
    auto* data =
        static_cast<LibTreeData*>(lib_tree_->GetItemData(evt.GetItem()));
    if (!data) return;
    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) return;
    std::vector<int> parent;
    if (ub.CurrentSelection().kind == UIBuilder::Selection::Kind::kElement) {
      parent = ub.CurrentSelection().element;
    }
    int idx = ub.Doc() ? static_cast<int>(ub.Doc()->root.children.size()) : 0;
    if (auto* p = ub.NodeAt(parent)) idx = static_cast<int>(p->children.size());

    if (!data->is_template) {
      ub.InsertNode(parent, idx, ub.NewNode(data->payload));
    } else {
      UI::UxmlNode inst;
      inst.type = "Instance";
      inst.attributes.push_back({"template", ub.RelativeToDocument(data->payload)});
      ub.InsertNode(parent, idx, std::move(inst));
    }
  });

  // --- Canvas events ---
  canvas_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(canvas_);
    const wxSize sz = canvas_->GetClientSize();
    dc.SetBrush(wxBrush(wxColour(24, 26, 30)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(0, 0, sz.x, sz.y);

    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
    if (!gc) return;

    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) return;

    const glm::vec2 vp(static_cast<float>(sz.x), static_cast<float>(sz.y));
    const glm::vec2 origin = ub.ComputeCanvasOrigin(vp);
    const glm::ivec2 csize = ub.CanvasSize();
    const float z = ub.Zoom();
    const float cw = csize.x * z;
    const float ch = csize.y * z;

    DrawCheckerboard(gc.get(), origin.x, origin.y, cw, ch, 10);

    // Draw background color behind UI render target
    const glm::vec4 bg = ub.Background();
    gc->SetBrush(wxBrush(wxColour(
        static_cast<unsigned char>(std::clamp(bg.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(bg.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(bg.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(std::clamp(bg.a, 0.0f, 1.0f) * 255.0f))));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRectangle(origin.x, origin.y, cw, ch);

    if (RenderHost* host = scene_.GetRenderHost()) {
      int pw = 0;
      int ph = 0;
      uint64_t fid = 0;
      if (host->CopyUIPreviewPixels(pw, ph, ui_pixels_, &fid) && pw > 0 &&
          ph > 0) {
        if (!ui_bmp_.IsOk() || fid != ui_frame_id_ ||
            ui_bmp_.GetWidth() != pw || ui_bmp_.GetHeight() != ph) {
          ui_frame_id_ = fid;
          ui_bmp_ = RgbaToWxBitmap(ui_pixels_.data(), pw, ph, true);
        }
      }
    }
    if (ui_bmp_.IsOk()) {
      gc->DrawBitmap(ui_bmp_, origin.x, origin.y, cw, ch);
    }

    WxOverlayCanvas overlay(gc.get(), overlay_font_);
    ub.RenderCanvasOverlay(overlay, origin);
  });

  canvas_->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent&) {
    canvas_hovered_ = true;
  });
  canvas_->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) {
    canvas_hovered_ = false;
  });

  canvas_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& evt) {
    canvas_->SetFocus();
    const wxPoint p = evt.GetPosition();
    local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
    prev_mouse_ = local_mouse_;
    mouse_valid_ = true;

    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) return;
    if (ub.PreviewMode()) {
      UI::UIInputEvent ev;
      ev.type = UI::UIInputEvent::Type::kPointerDown;
      ev.button = 0;
      pending_preview_events_.push_back(ev);
      return;
    }

    const wxSize sz = canvas_->GetClientSize();
    const glm::vec2 origin = ub.ComputeCanvasOrigin(
        {static_cast<float>(sz.x), static_cast<float>(sz.y)});
    canvas_dragging_ = true;
    if (!canvas_->HasCapture()) canvas_->CaptureMouse();
    ub.OnCanvasMouseDown(local_mouse_, origin, 0, evt.AltDown());
  });

  canvas_->Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& evt) {
    canvas_->SetFocus();
    const wxPoint p = evt.GetPosition();
    local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
    prev_mouse_ = local_mouse_;
    mouse_valid_ = true;
    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) return;
    const wxSize sz = canvas_->GetClientSize();
    const glm::vec2 origin = ub.ComputeCanvasOrigin(
        {static_cast<float>(sz.x), static_cast<float>(sz.y)});
    canvas_dragging_ = true;
    if (!canvas_->HasCapture()) canvas_->CaptureMouse();
    ub.OnCanvasMouseDown(local_mouse_, origin, 2, evt.AltDown());
  });

  canvas_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
    auto& ub = scene_.UIBuilderRef();
    if (ub.PreviewMode()) {
      UI::UIInputEvent ev;
      ev.type = UI::UIInputEvent::Type::kPointerUp;
      ev.button = 0;
      pending_preview_events_.push_back(ev);
    }
    if (canvas_dragging_) {
      canvas_dragging_ = false;
      ub.OnCanvasMouseUp();
    }
    if (canvas_->HasCapture()) canvas_->ReleaseMouse();
  });

  canvas_->Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent&) {
    if (canvas_dragging_) {
      canvas_dragging_ = false;
      scene_.UIBuilderRef().OnCanvasMouseUp();
    }
    if (canvas_->HasCapture()) canvas_->ReleaseMouse();
  });

  canvas_->Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
    canvas_dragging_ = false;
    scene_.UIBuilderRef().OnCanvasMouseUp();
  });

  canvas_->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt) {
    const wxPoint p = evt.GetPosition();
    local_mouse_ = {static_cast<float>(p.x), static_cast<float>(p.y)};
    const glm::vec2 delta =
        mouse_valid_ ? (local_mouse_ - prev_mouse_) : glm::vec2(0.0f);
    prev_mouse_ = local_mouse_;
    mouse_valid_ = true;

    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen() || ub.PreviewMode()) return;

    const wxSize sz = canvas_->GetClientSize();
    const glm::vec2 origin = ub.ComputeCanvasOrigin(
        {static_cast<float>(sz.x), static_cast<float>(sz.y)});
    if (canvas_dragging_ && ub.IsDraggingCanvas()) {
      ub.OnCanvasMouseDrag(local_mouse_, delta);
    } else {
      ub.UpdateHoveredElement(local_mouse_, origin);
      const auto h = ub.HitTestHandle(local_mouse_, origin);
      if (h == UIBuilder::Handle::kRight) {
        canvas_->SetCursor(wxCursor(wxCURSOR_SIZEWE));
      } else if (h == UIBuilder::Handle::kBottom) {
        canvas_->SetCursor(wxCursor(wxCURSOR_SIZENS));
      } else if (h == UIBuilder::Handle::kCorner) {
        canvas_->SetCursor(wxCursor(wxCURSOR_SIZENWSE));
      } else {
        canvas_->SetCursor(wxNullCursor);
      }
    }
  });

  canvas_->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& evt) {
    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) return;
    const int d = evt.GetWheelDelta();
    const float steps =
        d > 0 ? static_cast<float>(evt.GetWheelRotation()) / d : 0.0f;
    if (ub.PreviewMode()) {
      UI::UIInputEvent ev;
      ev.type = UI::UIInputEvent::Type::kWheel;
      ev.wheel = {0.0f, -steps * 3.0f};
      pending_preview_events_.push_back(ev);
      return;
    }
    ub.OnCanvasWheel(steps, 0.0f, evt.ControlDown());
  });

  canvas_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
    auto& ub = scene_.UIBuilderRef();
    if (!ub.IsOpen()) {
      evt.Skip();
      return;
    }
    if (ub.PreviewMode()) {
      QueuePreviewKey(evt, true);
      return;
    }
    if (evt.ControlDown()) {
      if (evt.GetKeyCode() == 'Z') { ub.Undo(); return; }
      if (evt.GetKeyCode() == 'Y') { ub.Redo(); return; }
      if (evt.GetKeyCode() == 'D') { ub.DuplicateSelected(); return; }
      if (evt.GetKeyCode() == 'S') { ub.Save(); return; }
    } else if (evt.GetKeyCode() == WXK_DELETE || evt.GetKeyCode() == WXK_BACK) {
      ub.DeleteSelected();
      return;
    }
    evt.Skip();
  });

  canvas_->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& evt) {
    if (scene_.UIBuilderRef().PreviewMode()) {
      QueuePreviewKey(evt, false);
      return;
    }
    evt.Skip();
  });

  canvas_->Bind(wxEVT_CHAR, [this](wxKeyEvent& evt) {
    if (!scene_.UIBuilderRef().PreviewMode()) {
      evt.Skip();
      return;
    }
    const wxChar ch = evt.GetUnicodeKey();
    if (ch != WXK_NONE && ch >= 32) {
      UI::UIInputEvent ev;
      ev.type = UI::UIInputEvent::Type::kChar;
      ev.character = static_cast<char32_t>(ch);
      ev.modifiers = WxMods(evt);
      pending_preview_events_.push_back(ev);
    }
  });

  canvas_->SetDropTarget(new CompositeDropTarget(
      nullptr,
      [this](wxCoord x, wxCoord y, const wxString& text) -> bool {
        auto& ub = scene_.UIBuilderRef();
        if (!ub.IsOpen()) return false;
        const wxSize sz = canvas_->GetClientSize();
        const glm::vec2 origin = ub.ComputeCanvasOrigin(
            {static_cast<float>(sz.x), static_cast<float>(sz.y)});
        const glm::vec2 pt(static_cast<float>(x), static_cast<float>(y));
        const std::string s = text.ToStdString();
        if (s.rfind(kLibPayloadPrefix, 0) == 0) {
          ub.DropNewTypeAtPoint(
              s.substr(std::char_traits<char>::length(kLibPayloadPrefix)), pt,
              origin);
          return true;
        }
        if (s.rfind(kTplPayloadPrefix, 0) == 0) {
          ub.DropTemplateAtPoint(
              s.substr(std::char_traits<char>::length(kTplPayloadPrefix)), pt,
              origin);
          return true;
        }
        if (auto res = ExtractResPathPayload(text)) {
          if (res->ends_with(".uxml")) {
            ub.DropTemplateAtPoint(*res, pt, origin);
            return true;
          }
          if (res->ends_with(".uss")) {
            ub.AddSheet(ub.RelativeToDocument(*res));
            return true;
          }
        }
        return false;
      }));
}

void UIBuilderPanel::QueuePreviewKey(wxKeyEvent& evt, bool down) {
  const UI::KeyCode kc = WxKeyToUiKey(evt.GetKeyCode());
  if (kc == UI::KeyCode::kNone) return;
  UI::UIInputEvent ev;
  ev.type = down ? UI::UIInputEvent::Type::kKeyDown
                 : UI::UIInputEvent::Type::kKeyUp;
  ev.key = kc;
  ev.modifiers = WxMods(evt);
  pending_preview_events_.push_back(ev);
}

void UIBuilderPanel::SyncFromController(float dt) {
  auto& ub = scene_.UIBuilderRef();
  const bool open = ub.IsOpen();
  const uint64_t doc_rev = ub.DocRevision();
  const uint64_t sel_rev = ub.SelectionRevision();

  if (open != last_open_) {
    RefreshAll();
  } else if (open) {
    if (doc_rev != last_doc_rev_) {
      RefreshTopBar();
      RefreshSheetsTree();
      RefreshHierarchyTree();
      RefreshCodeTabs();
      if (!dragging_) RefreshInspector();
      last_doc_rev_ = doc_rev;
      last_sel_rev_ = sel_rev;
    } else if (sel_rev != last_sel_rev_) {
      RefreshTopBar();
      RefreshSheetsTree();
      RefreshHierarchyTree();
      RefreshInspector();
      last_sel_rev_ = sel_rev;
    } else if (ub.PreviewMode() != last_preview_mode_) {
      RefreshTopBar();
    }
  }

  if (open) {
    const wxSize sz = canvas_->GetClientSize();
    ub.FitIfRequested({static_cast<float>(sz.x), static_cast<float>(sz.y)});
    UpdateCanvasInputPreview(dt);
    ub.UpdatePreview(scene_.GetRenderHost(), dt);
    canvas_->Refresh(false);
  }
}

void UIBuilderPanel::UpdateCanvasInputPreview(float) {
  auto& ub = scene_.UIBuilderRef();
  if (!ub.PreviewMode()) {
    pending_preview_events_.clear();
    return;
  }
  const wxSize sz = canvas_->GetClientSize();
  const glm::vec2 origin = ub.ComputeCanvasOrigin(
      {static_cast<float>(sz.x), static_cast<float>(sz.y)});
  const float z = std::max(0.01f, ub.Zoom());

  UI::UIInputFrame frame;
  frame.pointer = (local_mouse_ - origin) / z;
  frame.pointer_inside = canvas_hovered_;
  frame.mouse_available = canvas_hovered_;
  frame.keyboard_available = canvas_->HasFocus();
  frame.events = std::move(pending_preview_events_);
  pending_preview_events_.clear();
  ub.DispatchPreviewInput(frame);
}

void UIBuilderPanel::RefreshAll() {
  auto& ub = scene_.UIBuilderRef();
  last_open_ = ub.IsOpen();
  last_doc_rev_ = ub.DocRevision();
  last_sel_rev_ = ub.SelectionRevision();

  empty_banner_->Show(!last_open_);
  main_split_->Show(last_open_);
  RefreshTopBar();
  if (last_open_) {
    RefreshSheetsTree();
    RefreshHierarchyTree();
    RefreshLibrary();
    RefreshCodeTabs();
    RefreshInspector();
  }
  Layout();
}

void UIBuilderPanel::RefreshTopBar() {
  auto& ub = scene_.UIBuilderRef();
  const bool open = ub.IsOpen();
  last_preview_mode_ = ub.PreviewMode();

  if (!open) {
    file_lbl_->SetLabel("(no file)");
  } else {
    std::string label = fs::path(ub.path()).filename().string();
    if (ub.IsDirty()) label += " *";
    file_lbl_->SetLabel(wxString::FromUTF8(label));
  }

  save_btn_->Enable(open && ub.IsDirty());
  revert_btn_->Enable(open);
  close_btn_->Enable(open);
  undo_btn_->Enable(open && ub.CanUndo());
  redo_btn_->Enable(open && ub.CanRedo());
  const bool has_elem =
      open && ub.CurrentSelection().kind == UIBuilder::Selection::Kind::kElement;
  dup_btn_->Enable(has_elem);
  del_btn_->Enable(has_elem);
  preview_cb_->Enable(open);
  preview_cb_->SetValue(ub.PreviewMode());

  const glm::ivec2 csize = ub.CanvasSize();
  if (!canvas_w_->IsEditingText()) canvas_w_->SetValue(csize.x);
  if (!canvas_h_->IsEditingText()) canvas_h_->SetValue(csize.y);
  bg_swatch_->SetValue(ub.Background());

  int preset_sel = wxNOT_FOUND;
  for (size_t i = 0; i < std::size(kPresets); ++i) {
    if (kPresets[i].w == csize.x && kPresets[i].h == csize.y) {
      preset_sel = static_cast<int>(i);
      break;
    }
  }
  preset_choice_->SetSelection(preset_sel);

  // Themes dropdown
  theme_paths_.clear();
  wxArrayString tlabels;
  theme_paths_.push_back("");
  tlabels.Add("Default Theme");
  int tsel = ub.Theme().empty() ? 0 : wxNOT_FOUND;
  for (const auto& uss : scene_.ListProjectFiles({std::string(kUssExtension)})) {
    if (uss == ub.Theme()) tsel = static_cast<int>(theme_paths_.size());
    theme_paths_.push_back(uss);
    tlabels.Add(wxString::FromUTF8(fs::path(uss).filename().string()));
  }
  theme_choice_->Set(tlabels);
  if (tsel != wxNOT_FOUND) theme_choice_->SetSelection(tsel);
}

void UIBuilderPanel::RefreshSheetsTree() {
  syncing_trees_ = true;
  sheets_tree_->Freeze();
  sheets_tree_->DeleteAllItems();
  const wxTreeItemId root = sheets_tree_->AddRoot("Root");

  auto& ub = scene_.UIBuilderRef();
  const auto& sel = ub.CurrentSelection();
  wxTreeItemId to_select;

  for (int s = 0; s < static_cast<int>(ub.Sheets().size()); ++s) {
    const auto& entry = ub.Sheets()[s];
    std::string sname = fs::path(entry.src).filename().string();
    if (entry.dirty) sname += " *";
    wxTreeItemId s_item = sheets_tree_->AppendItem(
        root, wxString::FromUTF8(sname), -1, -1, new SheetTreeData(s, -1));
    if (entry.sheet) {
      for (int r = 0; r < static_cast<int>(entry.sheet->rules.size()); ++r) {
        const auto& rule = entry.sheet->rules[r];
        wxTreeItemId r_item = sheets_tree_->AppendItem(
            s_item, wxString::FromUTF8(rule.SelectorText()), -1, -1,
            new SheetTreeData(s, r));
        if (sel.kind == UIBuilder::Selection::Kind::kSelector &&
            sel.sheet == s && sel.rule == r) {
          to_select = r_item;
        }
      }
    }
    sheets_tree_->Expand(s_item);
  }
  if (to_select.IsOk()) sheets_tree_->SelectItem(to_select);
  sheets_tree_->Thaw();
  syncing_trees_ = false;
}

void UIBuilderPanel::RefreshHierarchyTree() {
  syncing_trees_ = true;
  hier_tree_->Freeze();
  hier_tree_->DeleteAllItems();
  const wxTreeItemId root = hier_tree_->AddRoot("Root");

  auto& ub = scene_.UIBuilderRef();
  const auto& sel = ub.CurrentSelection();
  if (ub.Doc()) {
    std::string doc_lbl =
        "Document (" + fs::path(ub.path()).filename().string() + ")";
    wxTreeItemId doc_item = hier_tree_->AppendItem(
        root, wxString::FromUTF8(doc_lbl), -1, -1,
        new ElemTreeData(true, {}));
    wxTreeItemId to_select;
    if (sel.kind == UIBuilder::Selection::Kind::kDocument) to_select = doc_item;

    auto add_node = [&](auto& self, const wxTreeItemId& parent_id,
                        const UI::UxmlNode& node,
                        std::vector<int> path) -> void {
      wxTreeItemId item = hier_tree_->AppendItem(
          parent_id, wxString::FromUTF8(UIBuilder::NodeLabel(node)), -1, -1,
          new ElemTreeData(false, path));
      if (sel.kind == UIBuilder::Selection::Kind::kElement &&
          sel.element == path) {
        to_select = item;
      }
      for (int i = 0; i < static_cast<int>(node.children.size()); ++i) {
        path.push_back(i);
        self(self, item, node.children[i], path);
        path.pop_back();
      }
      hier_tree_->Expand(item);
    };
    for (int i = 0; i < static_cast<int>(ub.Doc()->root.children.size()); ++i) {
      add_node(add_node, doc_item, ub.Doc()->root.children[i], {i});
    }
    hier_tree_->Expand(doc_item);
    if (to_select.IsOk()) hier_tree_->SelectItem(to_select);
  }
  hier_tree_->Thaw();
  syncing_trees_ = false;
}

void UIBuilderPanel::RefreshLibrary() {
  lib_tree_->Freeze();
  lib_tree_->DeleteAllItems();
  const wxTreeItemId root = lib_tree_->AddRoot("Root");
  const std::string filter = LowerStr(lib_search_->GetValue().ToStdString());

  std::string cur_cat;
  wxTreeItemId cat_item;
  for (const UI::UxmlTypeInfo* type : UI::UxmlFactoryRegistry::Instance().Types()) {
    if (!type) continue;
    if (!filter.empty() && LowerStr(type->name).find(filter) == std::string::npos) {
      continue;
    }
    if (!cat_item.IsOk() || type->category != cur_cat) {
      cur_cat = type->category;
      cat_item = lib_tree_->AppendItem(root, wxString::FromUTF8(cur_cat));
    }
    lib_tree_->AppendItem(cat_item, wxString::FromUTF8(type->name), -1, -1,
                          new LibTreeData(false, type->name));
  }

  auto& ub = scene_.UIBuilderRef();
  const auto uxmls = scene_.ListProjectFiles({".uxml"});
  wxTreeItemId tpl_cat;
  for (const auto& u : uxmls) {
    if (u == ub.path()) continue;
    const std::string fname = fs::path(u).stem().string();
    if (!filter.empty() && LowerStr(fname).find(filter) == std::string::npos) {
      continue;
    }
    if (!tpl_cat.IsOk()) {
      tpl_cat = lib_tree_->AppendItem(root, "Project Templates");
    }
    lib_tree_->AppendItem(tpl_cat, wxString::FromUTF8(fname), -1, -1,
                          new LibTreeData(true, u));
  }
  lib_tree_->ExpandAll();
  lib_tree_->Thaw();
}

void UIBuilderPanel::RefreshCodeTabs() {
  auto& ub = scene_.UIBuilderRef();
  if (!ub.IsOpen()) return;
  const int prev_sel = code_tabs_->GetSelection();
  code_tabs_->Freeze();
  code_tabs_->DeleteAllPages();

  auto add_code_page = [&](const wxString& title, const std::string& content) {
    auto* txt = new wxTextCtrl(
        code_tabs_, wxID_ANY, wxString::FromUTF8(content), wxDefaultPosition,
        wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL | wxBORDER_NONE);
    txt->SetFont(MonospaceFont());
    code_tabs_->AddPage(txt, title);
  };

  add_code_page("UXML", ub.WriteUxml());
  for (size_t i = 0; i < ub.Sheets().size(); ++i) {
    const std::string fname = fs::path(ub.Sheets()[i].src).filename().string();
    add_code_page(wxString::FromUTF8(fname), ub.WriteSheet(i));
  }
  if (prev_sel >= 0 && prev_sel < static_cast<int>(code_tabs_->GetPageCount())) {
    code_tabs_->SetSelection(prev_sel);
  }
  code_tabs_->Thaw();
}

void UIBuilderPanel::ScheduleInspectorRefresh() {
  if (inspector_rebuild_pending_) return;
  inspector_rebuild_pending_ = true;
  CallAfter([this]() {
    inspector_rebuild_pending_ = false;
    RefreshInspector();
  });
}

void UIBuilderPanel::RefreshInspector() {
  auto& ub = scene_.UIBuilderRef();
  const wxPoint prev_scroll = inspector_scroll_->GetViewStart();
  inspector_scroll_->Freeze();
  inspector_sizer_->Clear(true);

  UI::UxmlNode* node = ub.SelectedNode();
  UI::StyleRule* rule = ub.SelectedRule();
  const auto sel = ub.CurrentSelection();

  if (sel.kind == UIBuilder::Selection::Kind::kDocument) {
    auto* h = new wxStaticText(inspector_scroll_, wxID_ANY, "UI Document");
    h->SetFont(HeaderFont(1));
    inspector_sizer_->Add(h, 0, wxALL, 10);

    auto* p = new wxStaticText(inspector_scroll_, wxID_ANY,
                               wxString::FromUTF8(ub.path()));
    p->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    inspector_sizer_->Add(p, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

    auto* info = new wxStaticText(
        inspector_scroll_, wxID_ANY,
        "Configure canvas dimensions, background, and theme from the toolbar.");
    info->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    info->Wrap(250);
    inspector_sizer_->Add(info, 0, wxLEFT | wxRIGHT, 10);
  } else if (!node && !rule) {
    auto* msg = new wxStaticText(inspector_scroll_, wxID_ANY,
                                 "Select an element or selector.");
    msg->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    inspector_sizer_->Add(msg, 0, wxALL, 10);
  } else {
    // Selector or Element Header
    if (rule) {
      auto* h_lbl = new wxStaticText(inspector_scroll_, wxID_ANY, "Selector");
      inspector_sizer_->Add(h_lbl, 0, wxLEFT | wxRIGHT | wxTOP, 8);

      auto* s_edit = new wxTextCtrl(
          inspector_scroll_, wxID_ANY, wxString::FromUTF8(rule->SelectorText()),
          wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
      const int sh_idx = sel.sheet;
      s_edit->Bind(wxEVT_TEXT_ENTER, [this, s_edit, sh_idx](wxCommandEvent&) {
        auto& u = scene_.UIBuilderRef();
        if (UI::StyleRule* r = u.SelectedRule()) {
          u.RenameRule(r, sh_idx, s_edit->GetValue().ToStdString());
        }
      });
      inspector_sizer_->Add(s_edit, 0, wxEXPAND | wxALL, 8);
    } else if (node) {
      auto* hdr = new wxStaticText(
          inspector_scroll_, wxID_ANY,
          wxString::FromUTF8(UIBuilder::NodeLabel(*node)));
      hdr->SetFont(HeaderFont(1));
      inspector_sizer_->Add(hdr, 0, wxALL, 8);

      const UI::UxmlTypeInfo* type =
          UI::UxmlFactoryRegistry::Instance().Find(node->type);
      const auto elem_path = sel.element;

      // Classes Section
      auto* cls_sec = new CollapsibleSection(
          inspector_scroll_, "Classes",
          collapsed_groups_.count("__classes__") == 0, wxEmptyString, nullptr,
          [this](bool open) {
            if (open) collapsed_groups_.erase("__classes__");
            else collapsed_groups_.insert("__classes__");
            inspector_scroll_->FitInside();
          });
      {
        wxPanel* cb = cls_sec->Body();
        wxBoxSizer* cs = cls_sec->BodySizer();
        std::vector<std::string> classes;
        if (auto c = node->FindAttribute("class")) {
          std::istringstream in(*c);
          std::string tok;
          while (in >> tok) classes.push_back(tok);
        }

        auto* add_row = new wxBoxSizer(wxHORIZONTAL);
        auto* cls_in = new wxTextCtrl(cb, wxID_ANY, wxEmptyString,
                                      wxDefaultPosition, wxDefaultSize,
                                      wxTE_PROCESS_ENTER);
        cls_in->SetHint("class-name");
        auto* add_btn = new wxButton(cb, wxID_ANY, "Add", wxDefaultPosition,
                                     wxDefaultSize, wxBU_EXACTFIT);
        auto do_add_cls = [this, cls_in, elem_path, classes]() mutable {
          std::string name = cls_in->GetValue().ToStdString();
          if (name.starts_with('.')) name = name.substr(1);
          if (name.empty()) return;
          if (std::find(classes.begin(), classes.end(), name) == classes.end()) {
            classes.push_back(name);
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              scene_.UIBuilderRef().SetClassList(*n, classes);
            }
          }
        };
        cls_in->Bind(wxEVT_TEXT_ENTER, [do_add_cls](wxCommandEvent&) mutable {
          do_add_cls();
        });
        add_btn->Bind(wxEVT_BUTTON, [do_add_cls](wxCommandEvent&) mutable {
          do_add_cls();
        });
        add_row->Add(cls_in, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        add_row->Add(add_btn, 0);
        cs->Add(add_row, 0, wxEXPAND | wxBOTTOM, 4);

        auto* pills = new wxWrapSizer(wxHORIZONTAL);
        for (size_t i = 0; i < classes.size(); ++i) {
          auto* chip = new wxButton(
              cb, wxID_ANY, wxString::FromUTF8("." + classes[i] + "  x"),
              wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
          chip->Bind(wxEVT_BUTTON, [this, elem_path, classes, i](wxCommandEvent&) mutable {
            classes.erase(classes.begin() + static_cast<ptrdiff_t>(i));
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              scene_.UIBuilderRef().SetClassList(*n, classes);
            }
          });
          pills->Add(chip, 0, wxRIGHT | wxBOTTOM, 3);
        }
        cs->Add(pills, 0, wxEXPAND);

        if (!ub.InlineDeclarations(*node).empty()) {
          auto* ex_row = new wxBoxSizer(wxHORIZONTAL);
          auto* ex_in = new wxTextCtrl(cb, wxID_ANY);
          ex_in->SetHint("new-class");
          auto* ex_btn = new wxButton(cb, wxID_ANY, "Extract Styles",
                                      wxDefaultPosition, wxDefaultSize,
                                      wxBU_EXACTFIT);
          ex_btn->Bind(wxEVT_BUTTON, [this, ex_in, elem_path](wxCommandEvent&) {
            std::string nm = ex_in->GetValue().ToStdString();
            if (nm.starts_with('.')) nm = nm.substr(1);
            if (nm.empty()) return;
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              scene_.UIBuilderRef().ExtractInlineStylesToClass(*n, nm);
            }
          });
          ex_row->Add(ex_in, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
          ex_row->Add(ex_btn, 0);
          cs->Add(ex_row, 0, wxEXPAND | wxTOP, 6);
        }
      }
      cls_sec->FinishBuild();
      inspector_sizer_->Add(cls_sec, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                            6);

      // Attributes Section
      if (type) {
        auto* attr_sec = new CollapsibleSection(
            inspector_scroll_, "Attributes",
            collapsed_groups_.count("__attrs__") == 0, wxEmptyString, nullptr,
            [this](bool open) {
              if (open) collapsed_groups_.erase("__attrs__");
              else collapsed_groups_.insert("__attrs__");
              inspector_scroll_->FitInside();
            });
        wxPanel* ab = attr_sec->Body();
        wxBoxSizer* as = attr_sec->BodySizer();
        for (const auto& desc : type->attributes) {
          if (desc.name == "class" || desc.name == "style") continue;
          const auto* stored = node->FindAttribute(desc.name);
          const std::string val = stored ? *stored : desc.default_value;

          auto* row = new wxBoxSizer(wxHORIZONTAL);
          auto* lbl = new wxStaticText(ab, wxID_ANY,
                                       wxString::FromUTF8(desc.name),
                                       wxDefaultPosition, wxSize(108, -1),
                                       wxST_ELLIPSIZE_END);
          if (stored) lbl->SetFont(ab->GetFont().Bold());
          lbl->Bind(wxEVT_RIGHT_DOWN, [this, lbl, elem_path,
                                       name = desc.name, has = stored != nullptr](wxMouseEvent&) {
            wxMenu m;
            m.Append(1, "Unset")->Enable(has);
            m.Bind(wxEVT_MENU, [this, elem_path, name](wxCommandEvent&) {
              if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
                scene_.UIBuilderRef().UnsetAttribute(*n, name);
              }
            });
            lbl->PopupMenu(&m);
          });
          row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

          auto apply_attr = [this, elem_path, desc](const std::string& v,
                                                   bool coalesced) {
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              scene_.UIBuilderRef().SetAttribute(*n, desc.name, v,
                                                 desc.default_value, coalesced);
              last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
            }
          };

          wxWindow* ed = nullptr;
          switch (desc.type) {
            case FieldType::kBool: {
              auto* cb = new wxCheckBox(ab, wxID_ANY, wxEmptyString);
              cb->SetValue(val == "true" || val == "1");
              cb->Bind(wxEVT_CHECKBOX, [cb, apply_attr](wxCommandEvent&) {
                apply_attr(cb->GetValue() ? "true" : "false", false);
              });
              ed = cb;
              break;
            }
            case FieldType::kInt: {
              auto* dn = new DragNumberCtrl(ab, wxEmptyString, wxNullColour,
                                            std::atoi(val.c_str()), 1, true, 0);
              dn->SetCallbacks(
                  [this]() {
                    dragging_ = true;
                    scene_.UIBuilderRef().BeginEdit();
                  },
                  [apply_attr](double v) {
                    apply_attr(std::to_string(static_cast<int>(std::llround(v))),
                               true);
                  },
                  [this]() {
                    dragging_ = false;
                    scene_.UIBuilderRef().CommitEdit();
                    last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
                  });
              ed = dn;
              break;
            }
            case FieldType::kFloat: {
              auto* dn = new DragNumberCtrl(
                  ab, wxEmptyString, wxNullColour,
                  std::strtof(val.c_str(), nullptr), 0.1, false, 4);
              dn->SetCallbacks(
                  [this]() {
                    dragging_ = true;
                    scene_.UIBuilderRef().BeginEdit();
                  },
                  [apply_attr](double v) {
                    apply_attr(std::format("{}", static_cast<float>(v)), true);
                  },
                  [this]() {
                    dragging_ = false;
                    scene_.UIBuilderRef().CommitEdit();
                    last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
                  });
              ed = dn;
              break;
            }
            case FieldType::kEnum: {
              wxArrayString opts;
              int sel_idx = wxNOT_FOUND;
              for (size_t i = 0; i < desc.enum_values.size(); ++i) {
                opts.Add(wxString::FromUTF8(desc.enum_values[i]));
                if (desc.enum_values[i] == val) sel_idx = static_cast<int>(i);
              }
              auto* ch = new wxChoice(ab, wxID_ANY, wxDefaultPosition,
                                      wxDefaultSize, opts);
              if (sel_idx != wxNOT_FOUND) ch->SetSelection(sel_idx);
              ch->Bind(wxEVT_CHOICE, [ch, desc, apply_attr](wxCommandEvent&) {
                const int idx = ch->GetSelection();
                if (idx >= 0 && idx < static_cast<int>(desc.enum_values.size())) {
                  apply_attr(desc.enum_values[idx], false);
                }
              });
              ed = ch;
              break;
            }
            case FieldType::kColor: {
              UI::Color c =
                  UI::ParseColor(val).value_or(UI::Color::White());
              auto* sw = new ColorSwatchCtrl(ab, c.rgba);
              sw->SetOnChange([apply_attr](glm::vec4 rgba) {
                apply_attr(UI::FormatStyleValue(UI::StylePropertyId::kColor,
                                                UI::Color{rgba}),
                           false);
              });
              ed = sw;
              break;
            }
            default: {
              auto* txt = new wxTextCtrl(ab, wxID_ANY, wxString::FromUTF8(val),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxTE_PROCESS_ENTER);
              txt->Bind(wxEVT_TEXT_ENTER, [txt, apply_attr](wxCommandEvent&) {
                apply_attr(txt->GetValue().ToStdString(), false);
              });
              txt->Bind(wxEVT_KILL_FOCUS, [txt, apply_attr](wxFocusEvent& e) {
                apply_attr(txt->GetValue().ToStdString(), false);
                e.Skip();
              });
              ed = txt;
              break;
            }
          }
          if (ed) row->Add(ed, 1, wxEXPAND);
          as->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
        }
        attr_sec->FinishBuild();
        inspector_sizer_->Add(attr_sec, 0,
                              wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
      }

      // Bindings Section
      auto* bind_sec = new CollapsibleSection(
          inspector_scroll_, "Bindings",
          collapsed_groups_.count("__bindings__") == 0,
          node->bindings.empty()
              ? wxString()
              : wxString::Format("%zu", node->bindings.size()),
          nullptr,
          [this](bool open) {
            if (open) collapsed_groups_.erase("__bindings__");
            else collapsed_groups_.insert("__bindings__");
            inspector_scroll_->FitInside();
          });
      {
        wxPanel* bb = bind_sec->Body();
        wxBoxSizer* bs = bind_sec->BodySizer();
        for (size_t i = 0; i < node->bindings.size(); ++i) {
          const auto& b = node->bindings[i];
          auto* row = new wxBoxSizer(wxHORIZONTAL);
          auto* prop_in = new wxTextCtrl(bb, wxID_ANY,
                                         wxString::FromUTF8(b.property),
                                         wxDefaultPosition, wxSize(78, -1),
                                         wxTE_PROCESS_ENTER);
          prop_in->SetHint("prop");
          auto* path_in = new wxTextCtrl(bb, wxID_ANY,
                                         wxString::FromUTF8(b.data_source_path),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxTE_PROCESS_ENTER);
          path_in->SetHint("data.path");
          auto* rem = new wxButton(bb, wxID_ANY, "x", wxDefaultPosition,
                                   wxSize(22, 22), wxBU_EXACTFIT);

          auto commit_b = [this, elem_path, i, prop_in, path_in]() {
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              if (i < n->bindings.size()) {
                UI::UxmlBindingDesc upd = n->bindings[i];
                upd.property = prop_in->GetValue().ToStdString();
                upd.data_source_path = path_in->GetValue().ToStdString();
                scene_.UIBuilderRef().UpdateBinding(*n, i, upd);
                last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
              }
            }
          };
          prop_in->Bind(wxEVT_TEXT_ENTER, [commit_b](wxCommandEvent&) { commit_b(); });
          path_in->Bind(wxEVT_TEXT_ENTER, [commit_b](wxCommandEvent&) { commit_b(); });
          rem->Bind(wxEVT_BUTTON, [this, elem_path, i](wxCommandEvent&) {
            if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
              scene_.UIBuilderRef().RemoveBinding(*n, i);
            }
          });
          row->Add(prop_in, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
          row->Add(path_in, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
          row->Add(rem, 0, wxALIGN_CENTER_VERTICAL);
          bs->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
        }
        auto* add_b = new wxButton(bb, wxID_ANY, "+ Add Binding",
                                   wxDefaultPosition, wxDefaultSize,
                                   wxBU_EXACTFIT);
        add_b->Bind(wxEVT_BUTTON, [this, elem_path](wxCommandEvent&) {
          if (UI::UxmlNode* n = scene_.UIBuilderRef().NodeAt(elem_path)) {
            scene_.UIBuilderRef().AddBinding(*n);
          }
        });
        bs->Add(add_b, 0);
      }
      bind_sec->FinishBuild();
      inspector_sizer_->Add(bind_sec, 0,
                            wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

      // Matching Selectors Section
      const auto matches = ub.MatchingSelectors(ub.ElementFor(node));
      auto* match_sec = new CollapsibleSection(
          inspector_scroll_, "Matching Selectors",
          collapsed_groups_.count("__matching__") == 0,
          matches.empty() ? wxString()
                          : wxString::Format("%zu", matches.size()),
          nullptr,
          [this](bool open) {
            if (open) collapsed_groups_.erase("__matching__");
            else collapsed_groups_.insert("__matching__");
            inspector_scroll_->FitInside();
          });
      {
        wxPanel* mb = match_sec->Body();
        wxBoxSizer* ms = match_sec->BodySizer();
        if (matches.empty()) {
          auto* none = new wxStaticText(mb, wxID_ANY, "None");
          none->SetForegroundColour(
              wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
          ms->Add(none, 0);
        } else {
          for (const auto& m : matches) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* btn = new wxButton(
                mb, wxID_ANY, wxString::FromUTF8(m.selector_text),
                wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
            btn->Enable(m.sheet_index >= 0);
            btn->SetToolTip(wxString::FromUTF8(m.tooltip));
            btn->Bind(wxEVT_BUTTON, [this, s = m.sheet_index,
                                     r = m.rule_order](wxCommandEvent&) {
              scene_.UIBuilderRef().Select(
                  {UIBuilder::Selection::Kind::kSelector, {}, s, r});
            });
            auto* f = new wxStaticText(mb, wxID_ANY,
                                       wxString::FromUTF8(m.sheet_file));
            f->SetForegroundColour(
                wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            row->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
            row->Add(f, 1, wxALIGN_CENTER_VERTICAL);
            ms->Add(row, 0, wxEXPAND | wxBOTTOM, 3);
          }
        }
      }
      match_sec->FinishBuild();
      inspector_sizer_->Add(match_sec, 0,
                            wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // Style Groups (shared for element inline styles OR selector rule)
    std::vector<std::optional<UI::StyleValue>> declared;
    std::vector<std::string> declared_text;
    ub.ResolveDeclaredStyles(node, rule, declared, declared_text);
    UI::VisualElement* element = node ? ub.ElementFor(node) : nullptr;
    const auto elem_path = sel.element;
    const int sh_idx = sel.sheet;
    const int rl_idx = sel.rule;

    auto resolve_target = [this, is_rule = (rule != nullptr), elem_path,
                           sh_idx, rl_idx]()
        -> std::pair<UI::UxmlNode*, UI::StyleRule*> {
      auto& u = scene_.UIBuilderRef();
      if (is_rule) {
        if (sh_idx >= 0 && sh_idx < static_cast<int>(u.Sheets().size())) {
          auto& sh = u.Sheets()[sh_idx].sheet;
          if (sh && rl_idx >= 0 && rl_idx < static_cast<int>(sh->rules.size())) {
            return {nullptr, &sh->rules[rl_idx]};
          }
        }
        return {nullptr, nullptr};
      }
      return {u.NodeAt(elem_path), nullptr};
    };

    for (const auto& group : UIBuilder::StyleGroups()) {
      int set_count = 0;
      for (auto id : group.properties) {
        const size_t idx = static_cast<size_t>(id);
        if (declared[idx].has_value() || !declared_text[idx].empty()) {
          ++set_count;
        }
      }
      // Default style groups collapsed unless user already opened them
      const bool default_open = (group.name == std::string_view("Size") ||
                                 group.name == std::string_view("Flex"));
      const std::string gkey = std::string("sg:") + group.name;
      const bool is_open = default_open
                               ? (collapsed_groups_.count(gkey) == 0)
                               : (collapsed_groups_.count("open:" + gkey) > 0);
      if (default_open && is_open) {
      }
      auto* sec = new CollapsibleSection(
          inspector_scroll_, wxString::FromUTF8(group.name), is_open,
          set_count ? wxString::Format("%d", set_count) : wxString(), nullptr,
          [this, gkey, default_open](bool open) {
            if (default_open) {
              if (open) collapsed_groups_.erase(gkey);
              else collapsed_groups_.insert(gkey);
            } else {
              if (open) collapsed_groups_.insert("open:" + gkey);
              else collapsed_groups_.erase("open:" + gkey);
            }
            inspector_scroll_->FitInside();
          });

      wxPanel* gb = sec->Body();
      wxBoxSizer* gs = sec->BodySizer();
      for (auto id : group.properties) {
        const size_t i = static_cast<size_t>(id);
        const char* pname = UI::GetStylePropertyInfo(id).name;
        const bool is_set =
            declared[i].has_value() || !declared_text[i].empty();
        UI::StyleValue val =
            declared[i]   ? *declared[i]
            : element     ? element->resolvedStyle().Get(id)
                          : UI::DefaultStyleValue(id);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* lbl = new wxStaticText(gb, wxID_ANY, pname, wxDefaultPosition,
                                     wxSize(108, -1), wxST_ELLIPSIZE_END);
        if (is_set) lbl->SetFont(gb->GetFont().Bold());
        lbl->Bind(wxEVT_RIGHT_DOWN, [this, lbl, resolve_target, pname,
                                     is_set](wxMouseEvent&) {
          wxMenu m;
          m.Append(1, "Unset")->Enable(is_set);
          m.Bind(wxEVT_MENU, [this, resolve_target, pname](wxCommandEvent&) {
            auto [tn, tr] = resolve_target();
            if (tn || tr) scene_.UIBuilderRef().UnsetStyle(tn, tr, pname);
          });
          lbl->PopupMenu(&m);
        });
        row->Add(lbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

        auto apply_style = [this, lbl, resolve_target, pname](
                               const std::string& text_val, bool coalesced) {
          auto [tn, tr] = resolve_target();
          if (!tn && !tr) return;
          scene_.UIBuilderRef().SetStyle(tn, tr, pname, text_val, coalesced);
          lbl->SetFont(lbl->GetFont().Bold());
          lbl->Refresh();
          last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
        };

        wxWindow* ed = nullptr;
        if (UIBuilder::IsListValue(val) ||
            (!declared[i] && !declared_text[i].empty())) {
          const std::string t = !declared_text[i].empty()
                                    ? declared_text[i]
                                    : UI::FormatStyleValue(id, val);
          auto* tc = new wxTextCtrl(gb, wxID_ANY, wxString::FromUTF8(t),
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_PROCESS_ENTER);
          tc->Bind(wxEVT_TEXT_ENTER, [tc, apply_style](wxCommandEvent&) {
            apply_style(tc->GetValue().ToStdString(), false);
          });
          ed = tc;
        } else if (std::holds_alternative<UI::Color>(val)) {
          auto c = std::get<UI::Color>(val);
          auto* sw = new ColorSwatchCtrl(gb, c.rgba);
          sw->SetOnChange([id, apply_style](glm::vec4 rgba) {
            apply_style(UI::FormatStyleValue(id, UI::Color{rgba}), false);
          });
          ed = sw;
        } else if (std::holds_alternative<int>(val) &&
                   !UI::StyleKeywords(id).empty()) {
          const auto& kws = UI::StyleKeywords(id);
          wxArrayString opts;
          for (const auto& k : kws) opts.Add(wxString::FromUTF8(k));
          auto* ch =
              new wxChoice(gb, wxID_ANY, wxDefaultPosition, wxDefaultSize, opts);
          const int cur = std::get<int>(val);
          if (cur >= 0 && cur < static_cast<int>(kws.size())) {
            ch->SetSelection(cur);
          }
          ch->Bind(wxEVT_CHOICE, [ch, id, kws, apply_style](wxCommandEvent&) {
            const int idx = ch->GetSelection();
            if (idx >= 0 && idx < static_cast<int>(kws.size())) {
              apply_style(UI::FormatStyleValue(id, UI::StyleValue(idx)), false);
            }
          });
          ed = ch;
        } else if (std::holds_alternative<UI::Length>(val)) {
          auto len = std::get<UI::Length>(val);
          auto* lp = new wxPanel(gb, wxID_ANY);
          auto* ls = new wxBoxSizer(wxHORIZONTAL);
          auto* num = new DragNumberCtrl(lp, wxEmptyString, wxNullColour,
                                         len.value, 1.0, false, 2);
          wxArrayString units;
          units.Add("px");
          units.Add("%");
          units.Add("auto");
          const bool allow_none = (id == UI::StylePropertyId::kMaxWidth ||
                                   id == UI::StylePropertyId::kMaxHeight);
          if (allow_none) units.Add("none");
          auto* u_ch = new wxChoice(lp, wxID_ANY, wxDefaultPosition,
                                    wxSize(56, -1), units);
          int u_sel = 0;
          if (len.unit == UI::Length::Unit::kPercent) u_sel = 1;
          else if (len.unit == UI::Length::Unit::kAuto) u_sel = 2;
          else if (len.unit == UI::Length::Unit::kNone && allow_none) u_sel = 3;
          u_ch->SetSelection(u_sel);

          auto emit_len = [id, num, u_ch, apply_style](bool coalesced) {
            UI::Length out;
            out.value = static_cast<float>(num->GetValue());
            const int us = u_ch->GetSelection();
            if (us == 1) out.unit = UI::Length::Unit::kPercent;
            else if (us == 2) out.unit = UI::Length::Unit::kAuto;
            else if (us == 3) out.unit = UI::Length::Unit::kNone;
            else out.unit = UI::Length::Unit::kPixel;
            apply_style(UI::FormatStyleValue(id, UI::StyleValue(out)), coalesced);
          };
          num->SetCallbacks(
              [this]() {
                dragging_ = true;
                scene_.UIBuilderRef().BeginEdit();
              },
              [emit_len](double) { emit_len(true); },
              [this]() {
                dragging_ = false;
                scene_.UIBuilderRef().CommitEdit();
                last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
              });
          u_ch->Bind(wxEVT_CHOICE, [emit_len](wxCommandEvent&) {
            emit_len(false);
          });
          ls->Add(num, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
          ls->Add(u_ch, 0, wxALIGN_CENTER_VERTICAL);
          lp->SetSizer(ls);
          ed = lp;
        } else if (std::holds_alternative<float>(val)) {
          const float f = std::get<float>(val);
          const double step =
              id == UI::StylePropertyId::kOpacity ? 0.01 : 0.1;
          auto* dn = new DragNumberCtrl(gb, wxEmptyString, wxNullColour,
                                        f, step, false, 3);
          dn->SetCallbacks(
              [this]() {
                dragging_ = true;
                scene_.UIBuilderRef().BeginEdit();
              },
              [id, apply_style](double v) {
                apply_style(
                    UI::FormatStyleValue(id, UI::StyleValue(static_cast<float>(v))),
                    true);
              },
              [this]() {
                dragging_ = false;
                scene_.UIBuilderRef().CommitEdit();
                last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
              });
          ed = dn;
        } else if (std::holds_alternative<UI::Scale>(val)) {
          const auto sc = std::get<UI::Scale>(val);
          auto* vf = new Vec2Field(gb, sc.value, 0.02);
          vf->SetCallbacks(
              [this]() {
                dragging_ = true;
                scene_.UIBuilderRef().BeginEdit();
              },
              [id, apply_style](glm::vec2 v) {
                apply_style(UI::FormatStyleValue(id, UI::StyleValue(UI::Scale{v})),
                            true);
              },
              [this]() {
                dragging_ = false;
                scene_.UIBuilderRef().CommitEdit();
                last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
              });
          ed = vf;
        } else if (std::holds_alternative<UI::Rotate>(val)) {
          const auto rot = std::get<UI::Rotate>(val);
          auto* dn = new DragNumberCtrl(gb, wxEmptyString, wxNullColour,
                                        rot.degrees, 0.5, false, 2);
          dn->SetCallbacks(
              [this]() {
                dragging_ = true;
                scene_.UIBuilderRef().BeginEdit();
              },
              [id, apply_style](double v) {
                apply_style(UI::FormatStyleValue(
                                id, UI::StyleValue(UI::Rotate{static_cast<float>(v)})),
                            true);
              },
              [this]() {
                dragging_ = false;
                scene_.UIBuilderRef().CommitEdit();
                last_doc_rev_ = scene_.UIBuilderRef().DocRevision();
              });
          ed = dn;
        } else {
          auto* tc = new wxTextCtrl(
              gb, wxID_ANY, wxString::FromUTF8(UI::FormatStyleValue(id, val)),
              wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
          tc->Bind(wxEVT_TEXT_ENTER, [tc, apply_style](wxCommandEvent&) {
            apply_style(tc->GetValue().ToStdString(), false);
          });
          ed = tc;
        }

        if (ed) row->Add(ed, 1, wxEXPAND);
        gs->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
      }
      sec->FinishBuild();
      inspector_sizer_->Add(sec, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }

    // Rule custom variables (--name)
    if (rule) {
      auto* var_sec = new CollapsibleSection(
          inspector_scroll_, "Variables", true, wxEmptyString);
      wxPanel* vb = var_sec->Body();
      wxBoxSizer* vs = var_sec->BodySizer();
      for (size_t d_i = 0; d_i < rule->declarations.size(); ++d_i) {
        const auto& d = rule->declarations[d_i];
        if (!d.property.starts_with("--")) continue;
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* vl = new wxStaticText(vb, wxID_ANY, wxString::FromUTF8(d.property),
                                    wxDefaultPosition, wxSize(108, -1),
                                    wxST_ELLIPSIZE_END);
        auto* vc = new wxTextCtrl(vb, wxID_ANY, wxString::FromUTF8(d.value),
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER);
        auto* vrem = new wxButton(vb, wxID_ANY, "x", wxDefaultPosition,
                                  wxSize(22, 22), wxBU_EXACTFIT);

        const int sh_idx = sel.sheet;
        const int rl_idx = sel.rule;
        const std::string prop_name = d.property;
        vc->Bind(wxEVT_TEXT_ENTER,
                 [this, vc, sh_idx, rl_idx, prop_name](wxCommandEvent&) {
                   auto& u = scene_.UIBuilderRef();
                   if (sh_idx >= 0 &&
                       sh_idx < static_cast<int>(u.Sheets().size())) {
                     auto& sh = u.Sheets()[sh_idx].sheet;
                     if (sh && rl_idx >= 0 &&
                         rl_idx < static_cast<int>(sh->rules.size())) {
                       u.SetVariable(&sh->rules[rl_idx], sh_idx, prop_name,
                                     vc->GetValue().ToStdString());
                     }
                   }
                 });
        vrem->Bind(wxEVT_BUTTON, [this, sh_idx, rl_idx, d_i](wxCommandEvent&) {
          auto& u = scene_.UIBuilderRef();
          if (sh_idx >= 0 && sh_idx < static_cast<int>(u.Sheets().size())) {
            auto& sh = u.Sheets()[sh_idx].sheet;
            if (sh && rl_idx >= 0 &&
                rl_idx < static_cast<int>(sh->rules.size())) {
              u.RemoveVariable(&sh->rules[rl_idx], sh_idx, d_i);
            }
          }
        });
        row->Add(vl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        row->Add(vc, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        row->Add(vrem, 0, wxALIGN_CENTER_VERTICAL);
        vs->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
      }

      auto* add_var_row = new wxBoxSizer(wxHORIZONTAL);
      auto* new_v = new wxTextCtrl(vb, wxID_ANY);
      new_v->SetHint("--my-color");
      auto* add_vb = new wxButton(vb, wxID_ANY, "Add", wxDefaultPosition,
                                  wxDefaultSize, wxBU_EXACTFIT);
      const int sh_idx = sel.sheet;
      const int rl_idx = sel.rule;
      add_vb->Bind(wxEVT_BUTTON, [this, new_v, sh_idx, rl_idx](wxCommandEvent&) {
        auto& u = scene_.UIBuilderRef();
        if (sh_idx >= 0 && sh_idx < static_cast<int>(u.Sheets().size())) {
          auto& sh = u.Sheets()[sh_idx].sheet;
          if (sh && rl_idx >= 0 &&
              rl_idx < static_cast<int>(sh->rules.size())) {
            u.AddVariable(&sh->rules[rl_idx], sh_idx,
                          new_v->GetValue().ToStdString());
          }
        }
      });
      add_var_row->Add(new_v, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
      add_var_row->Add(add_vb, 0);
      vs->Add(add_var_row, 0, wxEXPAND | wxTOP, 4);

      var_sec->FinishBuild();
      inspector_sizer_->Add(var_sec, 0,
                            wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
    }
  }

  inspector_sizer_->Layout();
  inspector_scroll_->FitInside();
  inspector_scroll_->Scroll(prev_scroll.x, prev_scroll.y);
  inspector_scroll_->Thaw();
}

}  // namespace Mercury::EditorUI
