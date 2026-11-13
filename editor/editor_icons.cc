#include "editor_icons.h"

#include <wx/settings.h>

#include <string>
#include <unordered_map>

namespace Mercury::EditorUI {

namespace {

std::string HexColor(const wxColour& c) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.Red(), c.Green(),
                c.Blue());
  return std::string(buf);
}

std::string ReplaceAll(std::string s, const std::string& from,
                       const std::string& to) {
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

const char* SvgTemplate(EditorIcon icon) {
  switch (icon) {
    case EditorIcon::kSave:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linejoin='round' d='M2.5 2.5h8.5l2.5 2.5v8.5h-11z'/>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.3' x='5' y='2.5' "
             "width='5' height='3.5'/>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.3' x='4.5' "
             "y='9' width='7' height='4.5'/></svg>";

    case EditorIcon::kUndo:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M5.5 3.5L2 7l3.5 3.5'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' d='M2.5 7h7a4 4 0 0 1 0 8H7'/></svg>";

    case EditorIcon::kRedo:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M10.5 3.5L14 7l-3.5 3.5'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' d='M13.5 7h-7a4 4 0 0 0 0 8H9'/></svg>";

    case EditorIcon::kPlay:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<polygon fill='#2ea043' stroke='#238636' stroke-width='1.2' "
             "stroke-linejoin='round' points='4,2.5 13,8 4,13.5'/></svg>";

    case EditorIcon::kBuilding:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<circle fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-dasharray='16 8' cx='8' cy='8' r='5'/></svg>";

    case EditorIcon::kStop:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<rect fill='#d94a4a' stroke='#b83232' stroke-width='1.2' "
             "rx='1.2' x='3.5' y='3.5' width='9' height='9'/></svg>";

    case EditorIcon::kBuildScripts:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M5 4.5L2 8l3 3.5M11 4.5L14 8l-3 3.5M9 3l-2 10'/></svg>";

    case EditorIcon::kBuildGame:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<polygon fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linejoin='round' "
             "points='8,1.8 13.5,4.8 13.5,11.2 8,14.2 2.5,11.2 2.5,4.8'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.3' "
             "d='M2.5 4.8L8 7.8l5.5-3M8 7.8v6.4'/></svg>";

    case EditorIcon::kMove:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M8 2v12M2 8h12M6 4l2-2 2 2M6 12l2 2 2-2M4 6L2 8l2 2M12 "
             "6l2 2-2 2'/></svg>";

    case EditorIcon::kRotate:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M13 6.5A5.2 5.2 0 1 0 12.2 11'/>"
             "<polyline fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "points='10.2,6.5 13.2,6.8 13.5,3.8'/></svg>";

    case EditorIcon::kScale:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.4' "
             "x='2.5' y='7.5' width='6' height='6'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M9 7l4.5-4.5M10 2.5h3.5V6'/></svg>";

    case EditorIcon::kLocalSpace:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M4 12V3M4 12h9M4 12l4-4M2.5 4.5L4 2.5l1.5 2M11.5 10.5l2 "
             "1.5-2 1.5'/></svg>";

    case EditorIcon::kGlobalSpace:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<circle fill='none' stroke='%FG%' stroke-width='1.4' cx='8' "
             "cy='8' r='5.5'/>"
             "<ellipse fill='none' stroke='%FG%' stroke-width='1.2' cx='8' "
             "cy='8' rx='2.6' ry='5.5'/>"
             "<line stroke='%FG%' stroke-width='1.2' x1='2.5' y1='8' "
             "x2='13.5' y2='8'/></svg>";

    case EditorIcon::kView2D:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.5' x='3' y='3' "
             "width='10' height='10' rx='1'/>"
             "<line stroke='%FG%' stroke-width='1.3' x1='3' y1='8' x2='13' "
             "y2='8'/>"
             "<line stroke='%FG%' stroke-width='1.3' x1='8' y1='3' x2='8' "
             "y2='13'/></svg>";

    case EditorIcon::kView3D:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<polygon fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linejoin='round' "
             "points='8,2 13.5,5 13.5,11 8,14 2.5,11 2.5,5'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.3' "
             "d='M2.5 5L8 8l5.5-3M8 8v6'/></svg>";

    case EditorIcon::kFrame:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M2.5 5.5v-3h3M10.5 2.5h3v3M13.5 10.5v3h-3M5.5 "
             "13.5h-3v-3'/>"
             "<circle fill='%FG%' cx='8' cy='8' r='1.6'/></svg>";

    case EditorIcon::kCreate:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.8' "
             "stroke-linecap='round' d='M8 3v10M3 8h10'/></svg>";

    case EditorIcon::kClear:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' d='M2.5 4h11M2.5 8h7M2.5 12h5'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' d='M10 9l3.5 3.5M13.5 9L10 "
             "12.5'/></svg>";

    case EditorIcon::kSync:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M3 6h9.5L10 3.5M13 10H3.5L6 12.5'/></svg>";

    case EditorIcon::kPush:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M8 12V3.5M4.5 7L8 3.5 11.5 7M3 13.5h10'/></svg>";

    case EditorIcon::kRefresh:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M13.2 7.5A5.2 5.2 0 1 0 12 11.4'/>"
             "<polyline fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "points='10.5,7.5 13.5,7.8 13.8,4.8'/></svg>";

    case EditorIcon::kOptions:
    case EditorIcon::kPreferences:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' "
             "d='M2.5 4.5h11M2.5 8h11M2.5 11.5h11'/>"
             "<circle fill='%FG%' cx='5.5' cy='4.5' r='1.5'/>"
             "<circle fill='%FG%' cx='10.5' cy='8' r='1.5'/>"
             "<circle fill='%FG%' cx='6.5' cy='11.5' r='1.5'/></svg>";

    case EditorIcon::kRevert:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M2.8 7.5A5.2 5.2 0 1 1 4 11.4'/>"
             "<polyline fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "points='5.5,7.5 2.5,7.8 2.2,4.8'/></svg>";

    case EditorIcon::kDuplicate:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.4' x='5' y='5' "
             "width='8.5' height='8.5' rx='1'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linecap='round' d='M10.5 5V2.5h-8v8H5'/></svg>";

    case EditorIcon::kDelete:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M2.5 4.5h11M6 2.5h4M4.5 4.5l.7 9h5.6l.7-9'/></svg>";

    case EditorIcon::kPopOut:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M7 3.5H3.5v9h9V9M9 2.5h4.5V7M8 8l5.5-5.5'/></svg>";

    case EditorIcon::kDock:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<rect fill='none' stroke='%FG%' stroke-width='1.4' rx='1' "
             "x='2.5' y='2.5' width='11' height='11'/>"
             "<line stroke='%FG%' stroke-width='1.4' x1='2.5' y1='9.5' "
             "x2='13.5' y2='9.5'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M8 4v4M6 6.2L8 8.2l2-2'/></svg>";

    case EditorIcon::kClose:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.6' "
             "stroke-linecap='round' "
             "d='M4 4l8 8M12 4l-8 8'/></svg>";

    case EditorIcon::kCommit:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<polyline fill='none' stroke='%FG%' stroke-width='1.7' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "points='3,8.5 6.5,12 13,4.5'/></svg>";

    case EditorIcon::kFit:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.5' "
             "stroke-linecap='round' stroke-linejoin='round' "
             "d='M2.5 5.5v-3h3M10.5 2.5h3v3M13.5 10.5v3h-3M5.5 "
             "13.5h-3v-3M5.5 5.5h5v5h-5z'/></svg>";

    case EditorIcon::kOneToOne:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<circle fill='none' stroke='%FG%' stroke-width='1.5' cx='7' "
             "cy='7' r='4.2'/>"
             "<line stroke='%FG%' stroke-width='1.6' stroke-linecap='round' "
             "x1='10.2' y1='10.2' x2='13.5' y2='13.5'/></svg>";

    case EditorIcon::kOpenFolder:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linejoin='round' "
             "d='M2.5 3.5h4l1.5 1.5h5.5v7.5h-11z'/></svg>";

    case EditorIcon::kNewFile:
      return "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>"
             "<path fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linejoin='round' "
             "d='M3.5 2.5h6l3 3v8h-9zM9.5 2.5v3h3'/>"
             "<path fill='none' stroke='%FG%' stroke-width='1.4' "
             "stroke-linecap='round' d='M8 7.5v4M6 9.5h4'/></svg>";
  }
  return "";
}

}  // namespace

wxBitmapBundle GetEditorIcon(EditorIcon icon, int size) {
  const wxColour fg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT);
  const std::string fg_hex = HexColor(fg);
  const std::string key = std::to_string(static_cast<int>(icon)) + "_" +
                          std::to_string(size) + "_" + fg_hex;
  static std::unordered_map<std::string, wxBitmapBundle> cache;
  if (auto it = cache.find(key); it != cache.end()) {
    return it->second;
  }
  std::string svg = ReplaceAll(SvgTemplate(icon), "%FG%", fg_hex);
  wxBitmapBundle bundle =
      wxBitmapBundle::FromSVG(svg.c_str(), wxSize(size, size));
  cache.emplace(key, bundle);
  return bundle;
}

void ApplyButtonIconAndPadding(wxAnyButton* btn, EditorIcon icon,
                               int extra_pad_x, int icon_size, int margin_x) {
  if (!btn) return;
  btn->SetMinSize(wxDefaultSize);
  btn->SetBitmap(GetEditorIcon(icon, icon_size), wxLEFT);
  btn->SetBitmapMargins(wxSize(margin_x, 0));
  btn->InvalidateBestSize();
  const wxSize best = btn->GetBestSize();
  if (extra_pad_x > 0 && best.x > 0) {
    btn->SetMinSize(wxSize(best.x + extra_pad_x, best.y));
  }
}

void ApplyButtonPadding(wxAnyButton* btn, int extra_pad_x) {
  if (!btn) return;
  btn->SetMinSize(wxDefaultSize);
  btn->InvalidateBestSize();
  const wxSize best = btn->GetBestSize();
  if (extra_pad_x > 0 && best.x > 0) {
    btn->SetMinSize(wxSize(best.x + extra_pad_x, best.y));
  }
}

}  // namespace Mercury::EditorUI
