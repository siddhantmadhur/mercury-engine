#include "console_panel.h"

#include <algorithm>
#include <cctype>

#include "wx_theme.h"

namespace Mercury::EditorUI {

namespace {

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

const char* SourceTag(Console::Source src) {
  switch (src) {
    case Console::Source::kGame: return "[game]  ";
    case Console::Source::kBuild: return "[build] ";
    case Console::Source::kLore: return "[lore]  ";
    case Console::Source::kEditor:
    default: return "[editor]";
  }
}

}  // namespace

ConsolePanel::ConsolePanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY), scene_(scene) {
  ApplyDarkPalette(this);
  auto* root = new wxBoxSizer(wxVERTICAL);

  auto* header_wrap = new wxPanel(this, wxID_ANY);
  header_wrap->SetBackgroundColour(Colors::kBgHeader);
  auto* bar = new wxPanel(header_wrap, wxID_ANY);
  bar->SetBackgroundColour(Colors::kBgHeader);
  auto* bar_sizer = new wxBoxSizer(wxHORIZONTAL);

  auto* clear_btn = new wxButton(bar, wxID_ANY, "Clear", wxDefaultPosition,
                                 wxDefaultSize, wxBU_EXACTFIT);
  StyleButton(clear_btn);
  bar_sizer->Add(clear_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

  auto add_cb = [&](const char* label, bool initial) {
    auto* cb = new wxCheckBox(bar, wxID_ANY, label);
    cb->SetValue(initial);
    cb->SetForegroundColour(Colors::kTextPrimary);
    cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
      filter_dirty_ = true;
      RebuildList(false);
    });
    bar_sizer->Add(cb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    return cb;
  };
  show_info_ = add_cb("Info", true);
  show_warnings_ = add_cb("Warnings", true);
  show_errors_ = add_cb("Errors", true);
  show_build_ = add_cb("Build", true);
  show_game_ = add_cb("Game", true);
  show_lore_ = add_cb("Lore", true);

  search_ = new wxTextCtrl(bar, wxID_ANY);
  search_->SetHint("Filter console...");
  StyleTextCtrl(search_);
  search_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
    filter_dirty_ = true;
    RebuildList(false);
  });
  bar_sizer->Add(search_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

  bar->SetSizer(bar_sizer);
  auto* bar_wrap = new wxBoxSizer(wxVERTICAL);
  bar_wrap->Add(bar, 0, wxEXPAND | wxALL, 4);

  header_wrap->SetSizer(bar_wrap);
  root->Add(header_wrap, 0, wxEXPAND);

  list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                         wxLC_REPORT | wxLC_NO_HEADER | wxLC_SINGLE_SEL |
                             wxBORDER_NONE);
  list_->SetBackgroundColour(Colors::kBgCanvas);
  list_->SetForegroundColour(Colors::kTextPrimary);
  list_->SetFont(MonospaceFont());
  list_->InsertColumn(0, "Source", wxLIST_FORMAT_LEFT, 78);
  list_->InsertColumn(1, "Message", wxLIST_FORMAT_LEFT, 700);
  root->Add(list_, 1, wxEXPAND);

  SetSizer(root);

  clear_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    scene_.ConsoleRef().Clear();
    last_version_ = 0;
    RebuildList(false);
  });

  list_->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
    const int w = std::max(200, list_->GetClientSize().x - 94);
    list_->SetColumnWidth(1, w);
    evt.Skip();
  });

  list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent& evt) {
    const long idx = evt.GetIndex();
    if (idx < 0 || static_cast<size_t>(idx) >= visible_lines_.size()) return;
    if (auto script = Console::FindScriptFile(visible_lines_[idx].text);
        script && scene_.GetProject()) {
      scene_.OpenInExternalEditor(scene_.GetProject()->GetRoot() / *script);
    }
  });

  list_->Bind(wxEVT_MOTION, [this](wxMouseEvent& evt) {
    int flags = 0;
    const long idx = list_->HitTest(evt.GetPosition(), flags);
    if (idx >= 0 && static_cast<size_t>(idx) < visible_lines_.size()) {
      if (auto script = Console::FindScriptFile(visible_lines_[idx].text);
          script && scene_.GetProject()) {
        list_->SetCursor(wxCursor(wxCURSOR_HAND));
        list_->SetToolTip(wxString::Format("Double-click to open %s",
                                           wxString::FromUTF8(*script)));
        evt.Skip();
        return;
      }
    }
    list_->SetCursor(*wxSTANDARD_CURSOR);
    list_->UnsetToolTip();
    evt.Skip();
  });

  RebuildList(true);
}

void ConsolePanel::SyncFromController() {
  const size_t version = scene_.ConsoleRef().GetVersion();
  if (version != last_version_ || filter_dirty_) {
    RebuildList(version != last_version_);
  }
}

void ConsolePanel::RebuildList(bool force_scroll) {
  const size_t version = scene_.ConsoleRef().GetVersion();
  const long count_before = list_->GetItemCount();
  const long top_before = count_before > 0 ? list_->GetTopItem() : 0;
  const long per_page = std::max<long>(1, list_->GetCountPerPage());
  const bool was_at_bottom =
      count_before == 0 || (top_before + per_page >= count_before - 1);

  const std::string needle = ToLower(search_->GetValue().ToStdString());
  const auto snapshot = scene_.ConsoleRef().Snapshot();

  visible_lines_.clear();
  for (const auto& line : snapshot) {
    if (line.level == Console::Level::kInfo && !show_info_->GetValue()) continue;
    if (line.level == Console::Level::kWarning && !show_warnings_->GetValue())
      continue;
    if (line.level == Console::Level::kError && !show_errors_->GetValue())
      continue;
    if (line.source == Console::Source::kGame && !show_game_->GetValue())
      continue;
    if (line.source == Console::Source::kBuild && !show_build_->GetValue())
      continue;
    if (line.source == Console::Source::kLore && !show_lore_->GetValue())
      continue;
    if (!needle.empty() && ToLower(line.text).find(needle) == std::string::npos) {
      continue;
    }
    visible_lines_.push_back(line);
  }

  list_->Freeze();
  list_->DeleteAllItems();
  for (size_t i = 0; i < visible_lines_.size(); ++i) {
    const auto& line = visible_lines_[i];
    const long row = list_->InsertItem(static_cast<long>(i),
                                       wxString::FromUTF8(SourceTag(line.source)));
    list_->SetItem(row, 1, wxString::FromUTF8(line.text));
    if (line.level == Console::Level::kWarning) {
      list_->SetItemTextColour(row, wxColour(245, 200, 90));
    } else if (line.level == Console::Level::kError) {
      list_->SetItemTextColour(row, wxColour(245, 115, 115));
    } else {
      list_->SetItemTextColour(row, Colors::kTextPrimary);
    }
  }
  if (!visible_lines_.empty() && (force_scroll || was_at_bottom) &&
      was_at_bottom) {
    list_->EnsureVisible(static_cast<long>(visible_lines_.size() - 1));
  }
  list_->Thaw();

  last_version_ = version;
  filter_dirty_ = false;
}

}  // namespace Mercury::EditorUI
