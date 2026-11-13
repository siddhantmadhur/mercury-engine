/*
 * inspector_panel.h
 *
 * Retained-mode Inspector panel for the selected MapNode (components,
 * typed fields, events, lists, Rigidbody lock groups, UIDocument UI Builder
 * link, raw properties, Add Component) or selected asset (.masset / file).
 */
#ifndef MERCURY_EDITOR_INSPECTOR_PANEL_H
#define MERCURY_EDITOR_INSPECTOR_PANEL_H

#include <wx/scrolwin.h>
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "editor_scene.h"
#include "wx_theme.h"

namespace Mercury::EditorUI {

class InspectorPanel : public wxPanel {
 public:
  InspectorPanel(wxWindow* parent, EditorScene& scene);

  void SyncFromController();

 private:
  struct TransformBinding {
    std::string field_name;
    Vec3Field* ctrl = nullptr;
  };

  void ScheduleRebuild();
  void RebuildNow();
  void SyncTransformsInPlace();

  void BuildNodeInspector(MapNode& node);
  void BuildAssetInspector(const std::string& res_path);

  using WriteVariantFn = std::function<void(const mtext::Variant&, bool coalesced)>;

  wxWindow* CreateValueEditor(wxWindow* parent, const FieldInfo& field,
                              FieldType type, const mtext::Variant& current,
                              WriteVariantFn write_fn);
  wxPanel* CreateListEditor(wxWindow* parent, const FieldInfo& field,
                            const mtext::Variant& current,
                            WriteVariantFn write_fn);
  wxPanel* CreateEventCallsEditor(wxWindow* parent,
                                  const mtext::Variant& current,
                                  WriteVariantFn write_fn);
  wxWindow* CreateNodePicker(wxWindow* parent, const std::string& current_path,
                             std::function<void(const std::string&)> on_pick);
  wxWindow* CreateAssetPicker(wxWindow* parent, const std::string& current_path,
                              const std::string& asset_type,
                              std::function<void(const std::string&)> on_pick);
  void AddFieldRow(wxWindow* parent, wxBoxSizer* parent_sizer,
                   const FieldInfo& field, mtext::PropertyList& props,
                   bool* is_uidoc_source_btn_dirty,
                   std::function<void(bool coalesced)> on_prop_changed,
                   const std::string& comp_type);
  void AddLockGroupRow(wxWindow* parent, wxBoxSizer* parent_sizer,
                       const char* label, const std::string& prefix,
                       mtext::PropertyList& props,
                       std::function<void(bool coalesced)> on_prop_changed);
  void AddRawPropertyRow(wxWindow* parent, wxBoxSizer* parent_sizer,
                         const mtext::Property& prop,
                         std::function<void(const std::string& new_val_text)> on_commit,
                         std::function<void()> on_remove);

  EditorScene& scene_;
  wxScrolledWindow* scroll_ = nullptr;
  wxBoxSizer* content_sizer_ = nullptr;

  uint64_t last_map_rev_ = 0;
  uint64_t last_sel_rev_ = 0;
  uint64_t last_schema_rev_ = 0;
  uint64_t last_node_uid_ = 0;
  std::string last_asset_;
  bool rebuild_pending_ = false;
  bool dragging_ = false;

  std::set<std::string> collapsed_sections_;
  std::vector<TransformBinding> transform_bindings_;
};

}  // namespace Mercury::EditorUI

#endif  // MERCURY_EDITOR_INSPECTOR_PANEL_H
