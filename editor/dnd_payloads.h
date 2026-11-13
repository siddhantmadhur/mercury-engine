/*
 * dnd_payloads.h
 *
 * Shared internal & OS drag-and-drop payload helpers for wxWidgets editor
 * panels.
 */
#ifndef MERCURY_EDITOR_DND_PAYLOADS_H
#define MERCURY_EDITOR_DND_PAYLOADS_H

#include <wx/dataobj.h>
#include <wx/dnd.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Mercury::EditorUI {

inline constexpr const char* kResPayloadPrefix = "MERCURY_RES:";
inline constexpr const char* kNodePayloadPrefix = "MERCURY_NODE:";

inline wxDataObjectComposite* MakeResourceDragObject(
    const std::string& res_path, const std::filesystem::path& abs_path) {
  auto* composite = new wxDataObjectComposite();
  auto* text =
      new wxTextDataObject(wxString::FromUTF8(kResPayloadPrefix + res_path));
  composite->Add(text, /*preferred=*/true);
  if (!abs_path.empty()) {
    auto* files = new wxFileDataObject();
    files->AddFile(wxString::FromUTF8(abs_path.string()));
    composite->Add(files);
  }
  return composite;
}

inline wxTextDataObject* MakeNodeDragObject(uint64_t uid) {
  return new wxTextDataObject(wxString::FromUTF8(
      std::string(kNodePayloadPrefix) + std::to_string(uid)));
}

inline std::optional<std::string> ExtractResPathPayload(const wxString& text) {
  const std::string s = text.ToStdString();
  if (s.rfind(kResPayloadPrefix, 0) == 0) {
    return s.substr(std::char_traits<char>::length(kResPayloadPrefix));
  }
  return std::nullopt;
}

inline std::optional<uint64_t> ExtractNodeUidPayload(const wxString& text) {
  const std::string s = text.ToStdString();
  if (s.rfind(kNodePayloadPrefix, 0) == 0) {
    try {
      return std::stoull(
          s.substr(std::char_traits<char>::length(kNodePayloadPrefix)));
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Generic drop target accepting both OS files and internal text payloads.
class CompositeDropTarget : public wxDropTarget {
 public:
  using OnFilesFn = std::function<bool(const wxCoord x, const wxCoord y,
                                       const wxArrayString&)>;
  using OnTextFn =
      std::function<bool(const wxCoord x, const wxCoord y, const wxString&)>;

  CompositeDropTarget(OnFilesFn on_files, OnTextFn on_text)
      : on_files_(std::move(on_files)), on_text_(std::move(on_text)) {
    auto* comp = new wxDataObjectComposite();
    files_obj_ = new wxFileDataObject();
    text_obj_ = new wxTextDataObject();
    comp->Add(files_obj_);
    comp->Add(text_obj_, /*preferred=*/true);
    SetDataObject(comp);
  }

  wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def) override {
    if (!GetData()) return wxDragNone;
    auto* comp = static_cast<wxDataObjectComposite*>(GetDataObject());
    const wxDataFormat fmt = comp->GetReceivedFormat();
    if (fmt.GetType() == wxDF_FILENAME) {
      if (on_files_ && on_files_(x, y, files_obj_->GetFilenames())) {
        return def;
      }
      return wxDragNone;
    }
    if (on_text_ && on_text_(x, y, text_obj_->GetText())) {
      return def;
    }
    return wxDragNone;
  }

 private:
  OnFilesFn on_files_;
  OnTextFn on_text_;
  wxFileDataObject* files_obj_ = nullptr;
  wxTextDataObject* text_obj_ = nullptr;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_DND_PAYLOADS_H
