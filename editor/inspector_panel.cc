#include "inspector_panel.h"

#include <wx/menu.h>
#include <wx/statline.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "dialogs.h"
#include "dnd_payloads.h"
#include "project/asset_database.h"
#include "scripting/event.h"
#include "scripting/type_registry.h"

namespace Mercury::EditorUI {

namespace fs = std::filesystem;

namespace {

const std::vector<std::string> kImageExtensions = {".png", ".jpg", ".jpeg",
                                                   ".bmp", ".tga"};
const std::vector<std::string> kModelExtensions = {".obj", ".gltf", ".glb",
                                                   ".fbx", ".dae", ".ply"};
const std::vector<std::string> kFontExtensions = {".ttf", ".otf"};

std::string AssetLabel(const std::string& res_path) {
  if (res_path.empty()) return "(none)";
  return fs::path(res_path).filename().string();
}

}  // namespace

InspectorPanel::InspectorPanel(wxWindow* parent, EditorScene& scene)
    : wxPanel(parent, wxID_ANY), scene_(scene) {
  auto* root = new wxBoxSizer(wxVERTICAL);

  scroll_ = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxVSCROLL | wxBORDER_NONE);
  scroll_->SetScrollRate(0, 14);

  content_sizer_ = new wxBoxSizer(wxVERTICAL);
  scroll_->SetSizer(content_sizer_);

  root->Add(scroll_, 1, wxEXPAND);
  SetSizer(root);

  RebuildNow();
}

void InspectorPanel::SyncFromController() {
  const uint64_t map_rev = scene_.MapRevision();
  const uint64_t sel_rev = scene_.SelectionRevision();
  const uint64_t schema_rev = scene_.SchemaRevision();

  if (sel_rev != last_sel_rev_ || schema_rev != last_schema_rev_) {
    RebuildNow();
    return;
  }

  if (map_rev != last_map_rev_) {
    last_map_rev_ = map_rev;
    if (dragging_) {
      SyncTransformsInPlace();
      return;
    }
    const MapNode* node = scene_.GetSelectedNode();
    // During scene viewport gizmo drag or continuous transform updates, update
    // Vec3 fields in place instead of rebuilding the full panel tree.
    if (scene_.GetSelectedAsset().empty() && node &&
        node->uid == last_node_uid_ && !transform_bindings_.empty() &&
        scene_.IsDraggingHandle()) {
      SyncTransformsInPlace();
      return;
    }
    RebuildNow();
  }
}

void InspectorPanel::SyncTransformsInPlace() {
  const MapNode* node = scene_.GetSelectedNode();
  if (!node) return;
  const ComponentData* tr = node->FindComponent("Transform");
  if (!tr) return;
  for (const auto& binding : transform_bindings_) {
    if (!binding.ctrl || binding.ctrl->IsEditingText()) continue;
    const ComponentInfo* info =
        TypeRegistry::Instance().FindComponent("Transform");
    const FieldInfo* finfo =
        info ? info->FindField(binding.field_name) : nullptr;
    const mtext::Variant* stored = tr->properties.Get(binding.field_name);
    mtext::Variant val =
        stored ? *stored : (finfo ? finfo->default_value : mtext::Variant());
    if (const glm::vec3* v = val.Get<glm::vec3>()) {
      binding.ctrl->SetValue(*v);
    }
  }
}

void InspectorPanel::ScheduleRebuild() {
  if (rebuild_pending_) return;
  rebuild_pending_ = true;
  CallAfter([this]() {
    rebuild_pending_ = false;
    RebuildNow();
  });
}

void InspectorPanel::RebuildNow() {
  last_map_rev_ = scene_.MapRevision();
  last_sel_rev_ = scene_.SelectionRevision();
  last_schema_rev_ = scene_.SchemaRevision();
  last_asset_ = scene_.GetSelectedAsset();
  const MapNode* sel_node = scene_.GetSelectedNode();
  last_node_uid_ = sel_node ? sel_node->uid : 0;

  const wxPoint prev_scroll = scroll_->GetViewStart();

  scroll_->Freeze();
  transform_bindings_.clear();
  content_sizer_->Clear(true /*delete_windows*/);

  if (!last_asset_.empty()) {
    BuildAssetInspector(last_asset_);
  } else if (MapNode* node = scene_.GetSelectedNode()) {
    BuildNodeInspector(*node);
  } else {
    auto* empty = new wxStaticText(scroll_, wxID_ANY,
                                   "Select a node or an asset");
    empty->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    content_sizer_->Add(empty, 0, wxALL, 12);
  }

  content_sizer_->Layout();
  scroll_->FitInside();
  scroll_->Scroll(prev_scroll.x, prev_scroll.y);
  scroll_->Thaw();
}

void InspectorPanel::BuildAssetInspector(const std::string& res_path) {
  auto project = scene_.GetProject();
  if (!project) return;

  auto* header = new wxPanel(scroll_, wxID_ANY);
  auto* hs = new wxBoxSizer(wxVERTICAL);

  auto* title = new wxStaticText(
      header, wxID_ANY,
      wxString::FromUTF8(fs::path(res_path).filename().string()));
  title->SetFont(HeaderFont(1));
  hs->Add(title, 0, wxBOTTOM, 2);

  auto* sub = new wxStaticText(header, wxID_ANY, wxString::FromUTF8(res_path));
  sub->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  hs->Add(sub, 0);

  auto* hwrap = new wxBoxSizer(wxVERTICAL);
  hwrap->Add(hs, 0, wxEXPAND | wxALL, 8);
  header->SetSizer(hwrap);
  content_sizer_->Add(header, 0, wxEXPAND | wxBOTTOM, 6);

  if (!res_path.ends_with(kAssetExtension)) {
    auto* open_btn = new wxButton(scroll_, wxID_ANY, "Open in External Editor");
    open_btn->Bind(wxEVT_BUTTON, [this, project, res_path](wxCommandEvent&) {
      scene_.OpenInExternalEditor(project->Resolve(res_path));
    });
    content_sizer_->Add(open_btn, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    return;
  }

  auto asset = std::make_shared<Asset>();
  std::string error;
  if (!LoadAssetFile(project->Resolve(res_path), *asset, &error)) {
    auto* err = new wxStaticText(scroll_, wxID_ANY, wxString::FromUTF8(error));
    err->SetFont(err->GetFont().Bold());
    content_sizer_->Add(err, 0, wxALL, 8);
    return;
  }
  asset->path = res_path;

  auto* type_lbl = new wxStaticText(
      scroll_, wxID_ANY, wxString::FromUTF8("Type: " + asset->type));
  type_lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  content_sizer_->Add(type_lbl, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

  const AssetTypeInfo* info =
      TypeRegistry::Instance().FindAssetType(asset->type);

  auto save_asset = [this, asset]() { scene_.SaveAsset(*asset); };

  if (info) {
    for (const auto& field : info->fields) {
      AddFieldRow(
          scroll_, content_sizer_, field, asset->properties, nullptr,
          [save_asset](bool) { save_asset(); }, asset->type);
    }
  } else {
    auto* unk = new wxStaticText(scroll_, wxID_ANY, "Unknown asset type");
    unk->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    content_sizer_->Add(unk, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
  }

  for (const auto& property : asset->properties) {
    if (info && info->FindField(property.key)) continue;
    const std::string key = property.key;
    AddRawPropertyRow(
        scroll_, content_sizer_, property,
        [this, asset, key, save_asset](const std::string& val_text) {
          if (auto parsed = mtext::ParseValue(val_text)) {
            asset->properties.Set(key, *parsed);
            save_asset();
            last_map_rev_ = scene_.MapRevision();
          } else {
            scene_.ConsoleRef().Error("\"" + val_text +
                                      "\" is not a valid value");
          }
        },
        [this, asset, key, save_asset]() {
          asset->properties.Remove(key);
          save_asset();
          ScheduleRebuild();
        });
  }
}

void InspectorPanel::BuildNodeInspector(MapNode& node) {
  auto& registry = TypeRegistry::Instance();
  const uint64_t node_uid = node.uid;

  auto* header = new wxPanel(scroll_, wxID_ANY);
  auto* hs = new wxBoxSizer(wxVERTICAL);

  auto* name_ctrl = new wxTextCtrl(header, wxID_ANY,
                                   wxString::FromUTF8(node.name),
                                   wxDefaultPosition, wxDefaultSize,
                                   wxTE_PROCESS_ENTER);
  name_ctrl->SetFont(HeaderFont(1));
  auto commit_name = [this, node_uid, name_ctrl]() {
    MapNode* target = scene_.GetMap().FindByUid(node_uid);
    if (!target) return;
    const std::string n = name_ctrl->GetValue().ToStdString();
    if (!n.empty() && n != target->name) {
      scene_.RenameNode(target, n);
      ScheduleRebuild();
    } else {
      name_ctrl->ChangeValue(wxString::FromUTF8(target->name));
    }
  };
  name_ctrl->Bind(wxEVT_TEXT_ENTER, [commit_name](wxCommandEvent&) { commit_name(); });
  name_ctrl->Bind(wxEVT_KILL_FOCUS, [commit_name](wxFocusEvent& e) {
    commit_name();
    e.Skip();
  });
  hs->Add(name_ctrl, 0, wxEXPAND | wxBOTTOM, 4);

  auto* path_lbl = new wxStaticText(
      header, wxID_ANY, wxString::FromUTF8("Path: " + node.GetPath()),
      wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
  path_lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  hs->Add(path_lbl, 0, wxEXPAND);

  auto* hwrap = new wxBoxSizer(wxVERTICAL);
  hwrap->Add(hs, 0, wxEXPAND | wxALL, 8);
  header->SetSizer(hwrap);
  content_sizer_->Add(header, 0, wxEXPAND | wxBOTTOM, 6);

  for (size_t i = 0; i < node.components.size(); ++i) {
    ComponentData& component = node.components[i];
    const ComponentInfo* info = registry.FindComponent(component.type);

    std::string card_title = component.type;
    wxString badge;
    if (!info) {
      badge = "unknown";
    } else if (info->is_script) {
      badge = "script";
    }

    const std::string section_key =
        component.type + "#" + std::to_string(i);
    const bool expanded =
        collapsed_sections_.find(section_key) == collapsed_sections_.end();

    const bool removable = !info || info->removable;
    const size_t comp_count = node.components.size();
    auto* section = new CollapsibleSection(
        scroll_, wxString::FromUTF8(card_title), expanded, badge,
        [this, node_uid, i, removable, comp_count](const wxPoint& screen_pos) {
          wxMenu menu;
          constexpr int kIdRemove = 1001;
          constexpr int kIdUp = 1002;
          constexpr int kIdDown = 1003;
          constexpr int kIdReset = 1004;
          menu.Append(kIdRemove, "Remove Component")->Enable(removable);
          menu.Append(kIdUp, "Move Up")->Enable(i > 0);
          menu.Append(kIdDown, "Move Down")->Enable(i + 1 < comp_count);
          menu.AppendSeparator();
          menu.Append(kIdReset, "Reset to Defaults");

          menu.Bind(wxEVT_MENU, [this, node_uid, i](wxCommandEvent& evt) {
            MapNode* target = scene_.GetMap().FindByUid(node_uid);
            if (!target) return;
            switch (evt.GetId()) {
              case kIdRemove:
                scene_.RemoveComponent(target, i);
                ScheduleRebuild();
                break;
              case kIdUp:
                scene_.MoveComponent(target, i, i - 1);
                ScheduleRebuild();
                break;
              case kIdDown:
                scene_.MoveComponent(target, i, i + 1);
                ScheduleRebuild();
                break;
              case kIdReset:
                scene_.ResetComponentToDefaults(target, i);
                ScheduleRebuild();
                break;
            }
          });
          PopupMenu(&menu, ScreenToClient(screen_pos));
        },
        [this, section_key](bool is_open) {
          if (is_open) {
            collapsed_sections_.erase(section_key);
          } else {
            collapsed_sections_.insert(section_key);
          }
          scroll_->FitInside();
        });

    if (info && !info->description.empty()) {
      section->SetToolTip(wxString::FromUTF8(info->description));
    }

    wxPanel* body = section->Body();
    wxBoxSizer* bs = section->BodySizer();

    if (!info) {
      auto* msg = new wxStaticText(
          body, wxID_ANY,
          "This component type is not known. If it is a script, press Build. "
          "Its values are kept as they are.");
      msg->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
      msg->Wrap(260);
      bs->Add(msg, 0, wxEXPAND | wxBOTTOM, 6);
    }

    auto on_comp_changed = [this, node_uid, comp_type = component.type](bool coalesced) {
      if (comp_type == "Transform") {
        scene_.NotifyTransformEdited(node_uid, coalesced);
      } else {
        scene_.MarkChanged(true, coalesced);
      }
      last_map_rev_ = scene_.MapRevision();
    };

    wxButton* uidoc_btn = nullptr;
    if (info) {
      for (const auto& field : info->fields) {
        if (component.type == "Rigidbody") {
          if (field.name == "lock_translation_x") {
            AddLockGroupRow(body, bs, "Lock Transform", "lock_translation",
                            component.properties, on_comp_changed);
            continue;
          }
          if (field.name == "lock_translation_y" ||
              field.name == "lock_translation_z") {
            continue;
          }
          if (field.name == "lock_rotation_x") {
            AddLockGroupRow(body, bs, "Lock Rotation", "lock_rotation",
                            component.properties, on_comp_changed);
            continue;
          }
          if (field.name == "lock_rotation_y" ||
              field.name == "lock_rotation_z") {
            continue;
          }
        }
        bool uidoc_src_changed = false;
        AddFieldRow(body, bs, field, component.properties, &uidoc_src_changed,
                    on_comp_changed, component.type);
      }
    }

    if (component.type == "UIDocument") {
      const auto* source = component.properties.Get("source_asset");
      const auto* resource =
          source ? source->Get<mtext::Resource>() : nullptr;
      std::string ui_path = resource ? resource->path : "";
      uidoc_btn = new wxButton(body, wxID_ANY, "Edit in UI Builder");
      uidoc_btn->Enable(!ui_path.empty());
      uidoc_btn->Bind(wxEVT_BUTTON, [this, node_uid, i](wxCommandEvent&) {
        MapNode* target = scene_.GetMap().FindByUid(node_uid);
        if (!target || i >= target->components.size()) return;
        const auto* src = target->components[i].properties.Get("source_asset");
        const auto* res = src ? src->Get<mtext::Resource>() : nullptr;
        if (res && !res->path.empty()) {
          scene_.OpenInUIBuilder(res->path);
        }
      });
      bs->Add(uidoc_btn, 0, wxEXPAND | wxTOP, 6);
    }

    // Leftover unknown raw properties.
    for (const auto& property : component.properties) {
      if (info && info->FindField(property.key)) continue;
      if (component.type == "Rigidbody" &&
          (property.key == "allowed_dofs" ||
           property.key == "lock_transform_x" ||
           property.key == "lock_transform_y" ||
           property.key == "lock_transform_z" ||
           property.key == "lock_position_x" ||
           property.key == "lock_position_y" ||
           property.key == "lock_position_z")) {
        continue;
      }
      const std::string prop_key = property.key;
      AddRawPropertyRow(
          body, bs, property,
          [this, node_uid, i, prop_key](const std::string& val_text) {
            MapNode* target = scene_.GetMap().FindByUid(node_uid);
            if (!target || i >= target->components.size()) return;
            if (auto parsed = mtext::ParseValue(val_text)) {
              target->components[i].properties.Set(prop_key, *parsed);
              scene_.MarkChanged(true);
              last_map_rev_ = scene_.MapRevision();
            } else {
              scene_.ConsoleRef().Error("\"" + val_text +
                                        "\" is not a valid value");
            }
          },
          [this, node_uid, i, prop_key]() {
            MapNode* target = scene_.GetMap().FindByUid(node_uid);
            if (!target || i >= target->components.size()) return;
            target->components[i].properties.Remove(prop_key);
            scene_.MarkChanged(true);
            ScheduleRebuild();
          });
    }

    section->FinishBuild();
    content_sizer_->Add(section, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);
  }

  auto* add_btn = new wxButton(scroll_, wxID_ANY, "Add Component");
  add_btn->SetMinSize(wxSize(200, 28));
  add_btn->Bind(wxEVT_BUTTON, [this, node_uid](wxCommandEvent&) {
    MapNode* target = scene_.GetMap().FindByUid(node_uid);
    if (!target) return;
    auto picked = ShowAddComponentDialog(this, *target);
    if (!picked) return;
    if (*picked == "__NEW_SCRIPT__") {
      if (auto cls = ShowNewScriptDialog(this)) {
        scene_.CreateScript(*cls);
      }
      return;
    }
    target = scene_.GetMap().FindByUid(node_uid);
    if (target && scene_.AddComponent(target, *picked)) {
      ScheduleRebuild();
    }
  });
  content_sizer_->Add(add_btn, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM,
                      10);
}

void InspectorPanel::AddFieldRow(
    wxWindow* parent, wxBoxSizer* parent_sizer, const FieldInfo& field,
    mtext::PropertyList& props, bool* /*is_uidoc*/,
    std::function<void(bool coalesced)> on_prop_changed,
    const std::string& comp_type) {
  const mtext::Variant* stored = props.Get(field.name);
  mtext::Variant value = stored ? *stored : field.default_value;
  const bool overridden = stored && !(*stored == field.default_value);

  auto* row = new wxBoxSizer(wxHORIZONTAL);

  auto* label = new wxStaticText(parent, wxID_ANY,
                                 wxString::FromUTF8(field.name),
                                 wxDefaultPosition, wxSize(118, -1),
                                 wxST_ELLIPSIZE_END);
  if (overridden) label->SetFont(parent->GetFont().Bold());
  if (!field.tooltip.empty()) {
    label->SetToolTip(wxString::FromUTF8(field.tooltip));
  }
  label->Bind(wxEVT_RIGHT_DOWN,
              [this, label, &props, field, on_prop_changed](wxMouseEvent&) {
                wxMenu menu;
                constexpr int kIdReset = 1;
                const bool has_stored = props.Get(field.name) != nullptr;
                menu.Append(kIdReset, "Reset to Default")->Enable(has_stored);
                menu.Bind(wxEVT_MENU, [&](wxCommandEvent&) {
                  props.Remove(field.name);
                  on_prop_changed(false);
                  ScheduleRebuild();
                });
                label->PopupMenu(&menu);
              });

  // For lists and events, put the label above or on the left; left aligns cleanly.
  row->Add(label, 0, wxALIGN_TOP | wxTOP | wxRIGHT, 3);

  auto write_fn = [this, label, parent, &props, field, on_prop_changed,
                   comp_type](const mtext::Variant& new_val, bool coalesced) {
    if (new_val == field.default_value) {
      props.Remove(field.name);
    } else {
      props.Set(field.name, new_val);
    }
    const mtext::Variant* cur = props.Get(field.name);
    const bool now_over = cur && !(*cur == field.default_value);
    label->SetFont(now_over ? parent->GetFont().Bold() : parent->GetFont());
    label->Refresh();
    on_prop_changed(coalesced);

    // Structural UI changes (UIDocument source_asset updates "Edit in UI Builder"
    // button state; Lists/Events rebuild their child rows inside their own editor).
    if (comp_type == "UIDocument" && field.name == "source_asset" && !coalesced) {
      ScheduleRebuild();
    }
  };

  wxWindow* editor =
      CreateValueEditor(parent, field, field.type, value, write_fn);
  if (comp_type == "Transform" && field.type == FieldType::kVector3) {
    if (auto* vec = dynamic_cast<Vec3Field*>(editor)) {
      transform_bindings_.push_back({field.name, vec});
    }
  }
  row->Add(editor, 1, wxEXPAND);
  parent_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 5);
}

wxWindow* InspectorPanel::CreateValueEditor(wxWindow* parent,
                                            const FieldInfo& field,
                                            FieldType type,
                                            const mtext::Variant& current,
                                            WriteVariantFn write_fn) {
  switch (type) {
    case FieldType::kBool: {
      auto* cb = new wxCheckBox(parent, wxID_ANY, wxEmptyString);
      cb->SetValue(current.ToBool().value_or(false));
      cb->Bind(wxEVT_CHECKBOX, [cb, write_fn](wxCommandEvent&) {
        write_fn(mtext::Variant(cb->GetValue()), false);
      });
      return cb;
    }
    case FieldType::kInt: {
      const int v = static_cast<int>(current.ToInt().value_or(0));
      auto* ctrl = new DragNumberCtrl(parent, wxEmptyString, wxNullColour,
                                      static_cast<double>(v), 1.0, true, 0);
      ctrl->SetCallbacks(
          [this]() {
            dragging_ = true;
            scene_.BeginEdit();
          },
          [write_fn](double val) {
            write_fn(mtext::Variant(static_cast<int64_t>(std::llround(val))),
                     true);
          },
          [this]() {
            dragging_ = false;
            scene_.CommitEdit();
            last_map_rev_ = scene_.MapRevision();
          });
      return ctrl;
    }
    case FieldType::kFloat: {
      const double v = current.ToFloat().value_or(0.0);
      auto* ctrl = new DragNumberCtrl(parent, wxEmptyString, wxNullColour,
                                      v, 0.05, false, 4);
      ctrl->SetCallbacks(
          [this]() {
            dragging_ = true;
            scene_.BeginEdit();
          },
          [write_fn](double val) { write_fn(mtext::Variant(val), true); },
          [this]() {
            dragging_ = false;
            scene_.CommitEdit();
            last_map_rev_ = scene_.MapRevision();
          });
      return ctrl;
    }
    case FieldType::kString: {
      const std::string s = current.ToString().value_or("");
      auto* ctrl = new wxTextCtrl(parent, wxID_ANY, wxString::FromUTF8(s),
                                  wxDefaultPosition, wxDefaultSize,
                                  wxTE_PROCESS_ENTER);
      auto commit = [ctrl, write_fn]() {
        write_fn(mtext::Variant(ctrl->GetValue().ToStdString()), false);
      };
      ctrl->Bind(wxEVT_TEXT_ENTER, [commit](wxCommandEvent&) { commit(); });
      ctrl->Bind(wxEVT_KILL_FOCUS, [commit](wxFocusEvent& e) {
        commit();
        e.Skip();
      });
      return ctrl;
    }
    case FieldType::kVector2: {
      glm::vec2 v =
          current.Get<glm::vec2>() ? *current.Get<glm::vec2>() : glm::vec2(0.0f);
      auto* ctrl = new Vec2Field(parent, v, 0.05);
      ctrl->SetCallbacks(
          [this]() {
            dragging_ = true;
            scene_.BeginEdit();
          },
          [write_fn](glm::vec2 val) { write_fn(mtext::Variant(val), true); },
          [this]() {
            dragging_ = false;
            scene_.CommitEdit();
            last_map_rev_ = scene_.MapRevision();
          });
      return ctrl;
    }
    case FieldType::kVector3: {
      glm::vec3 v =
          current.Get<glm::vec3>() ? *current.Get<glm::vec3>() : glm::vec3(0.0f);
      auto* ctrl = new Vec3Field(parent, v, 0.05);
      ctrl->SetCallbacks(
          [this]() {
            dragging_ = true;
            scene_.BeginEdit();
          },
          [write_fn](glm::vec3 val) { write_fn(mtext::Variant(val), true); },
          [this]() {
            dragging_ = false;
            scene_.CommitEdit();
            last_map_rev_ = scene_.MapRevision();
          });
      return ctrl;
    }
    case FieldType::kColor: {
      glm::vec4 v = current.Get<mtext::Color>()
                        ? current.Get<mtext::Color>()->rgba
                        : glm::vec4(1.0f);
      auto* ctrl = new ColorSwatchCtrl(parent, v);
      ctrl->SetOnChange([write_fn](glm::vec4 rgba) {
        write_fn(mtext::Variant(mtext::Color{rgba}), false);
      });
      return ctrl;
    }
    case FieldType::kEnum: {
      wxArrayString choices;
      int sel = wxNOT_FOUND;
      const std::string cur = current.ToString().value_or("");
      for (size_t i = 0; i < field.enum_values.size(); ++i) {
        choices.Add(wxString::FromUTF8(field.enum_values[i]));
        if (field.enum_values[i] == cur) sel = static_cast<int>(i);
      }
      auto* choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, choices);
      if (sel != wxNOT_FOUND) choice->SetSelection(sel);
      choice->Bind(wxEVT_CHOICE, [choice, field, write_fn](wxCommandEvent&) {
        const int idx = choice->GetSelection();
        if (idx >= 0 && idx < static_cast<int>(field.enum_values.size())) {
          write_fn(mtext::Variant(field.enum_values[idx]), false);
        }
      });
      return choice;
    }
    case FieldType::kNodeRef: {
      const std::string cur = current.Get<mtext::NodePath>()
                                  ? current.Get<mtext::NodePath>()->path
                                  : current.ToString().value_or("");
      return CreateNodePicker(
          parent, cur, [write_fn](const std::string& chosen) {
            write_fn(mtext::Variant(mtext::NodePath{chosen}), false);
          });
    }
    case FieldType::kAsset: {
      const std::string cur = current.Get<mtext::Resource>()
                                  ? current.Get<mtext::Resource>()->path
                                  : current.ToString().value_or("");
      return CreateAssetPicker(
          parent, cur, field.asset_type, [write_fn](const std::string& chosen) {
            write_fn(mtext::Variant(mtext::Resource{chosen}), false);
          });
    }
    case FieldType::kList:
      return CreateListEditor(parent, field, current, write_fn);
    case FieldType::kEvent:
      return CreateEventCallsEditor(parent, current, write_fn);
  }
  return new wxPanel(parent, wxID_ANY);
}

wxWindow* InspectorPanel::CreateNodePicker(
    wxWindow* parent, const std::string& current_path,
    std::function<void(const std::string&)> on_pick) {
  auto paths = std::make_shared<std::vector<std::string>>();
  paths->push_back("");
  wxArrayString labels;
  labels.Add("(none)");
  int sel = current_path.empty() ? 0 : wxNOT_FOUND;

  scene_.GetMap().Visit([&](const MapNode& n) {
    const std::string p = n.GetPath();
    if (p == current_path) sel = static_cast<int>(paths->size());
    paths->push_back(p);
    labels.Add(wxString::FromUTF8(p));
  });
  if (sel == wxNOT_FOUND && !current_path.empty()) {
    sel = static_cast<int>(paths->size());
    paths->push_back(current_path);
    labels.Add(wxString::FromUTF8(current_path + " (missing)"));
  }

  auto* choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition,
                              wxDefaultSize, labels);
  if (sel != wxNOT_FOUND) choice->SetSelection(sel);
  if (!current_path.empty()) {
    choice->SetToolTip(wxString::FromUTF8(current_path));
  }

  choice->Bind(wxEVT_CHOICE, [choice, paths, on_pick](wxCommandEvent&) {
    const int idx = choice->GetSelection();
    if (idx >= 0 && idx < static_cast<int>(paths->size())) {
      on_pick((*paths)[idx]);
    }
  });

  choice->SetDropTarget(new CompositeDropTarget(
      nullptr,
      [this, on_pick](wxCoord, wxCoord, const wxString& text) -> bool {
        if (auto uid = ExtractNodeUidPayload(text)) {
          if (const MapNode* n = scene_.GetMap().FindByUid(*uid)) {
            on_pick(n->GetPath());
            ScheduleRebuild();
            return true;
          }
        }
        return false;
      }));

  return choice;
}

wxWindow* InspectorPanel::CreateAssetPicker(
    wxWindow* parent, const std::string& current_path,
    const std::string& asset_type,
    std::function<void(const std::string&)> on_pick) {
  auto files = std::make_shared<std::vector<std::string>>();
  files->push_back("");
  wxArrayString labels;
  labels.Add("(none)");

  const bool images = asset_type == "Texture";
  const bool models = asset_type == "Model";
  const bool fonts = asset_type == "Font";
  const bool uxml = asset_type == "UXML";
  const bool uss = asset_type == "StyleSheet";
  const bool plain = images || models || fonts || uxml || uss;

  const auto found =
      images   ? scene_.ListProjectFiles(kImageExtensions)
      : models ? scene_.ListProjectFiles(kModelExtensions)
      : fonts  ? scene_.ListProjectFiles(kFontExtensions)
      : uxml   ? scene_.ListProjectFiles({std::string(kUxmlExtension)})
      : uss    ? scene_.ListProjectFiles({std::string(kUssExtension)})
               : scene_.ListProjectFiles({std::string(kAssetExtension)});

  int sel = current_path.empty() ? 0 : wxNOT_FOUND;
  for (const auto& f : found) {
    if (!plain && !asset_type.empty() && scene_.Assets().TypeOf(f) != asset_type) {
      continue;
    }
    if (f == current_path) sel = static_cast<int>(files->size());
    files->push_back(f);
    labels.Add(wxString::FromUTF8(AssetLabel(f)));
  }
  if (sel == wxNOT_FOUND && !current_path.empty()) {
    sel = static_cast<int>(files->size());
    files->push_back(current_path);
    labels.Add(wxString::FromUTF8(AssetLabel(current_path)));
  }

  auto* choice = new wxChoice(parent, wxID_ANY, wxDefaultPosition,
                              wxDefaultSize, labels);
  if (sel != wxNOT_FOUND) choice->SetSelection(sel);
  if (!current_path.empty()) {
    choice->SetToolTip(wxString::FromUTF8(current_path));
  }

  choice->Bind(wxEVT_CHOICE, [choice, files, on_pick](wxCommandEvent&) {
    const int idx = choice->GetSelection();
    if (idx >= 0 && idx < static_cast<int>(files->size())) {
      on_pick((*files)[idx]);
    }
  });

  choice->SetDropTarget(new CompositeDropTarget(
      nullptr,
      [this, on_pick](wxCoord, wxCoord, const wxString& text) -> bool {
        if (auto res = ExtractResPathPayload(text)) {
          on_pick(*res);
          ScheduleRebuild();
          return true;
        }
        return false;
      }));

  return choice;
}

wxPanel* InspectorPanel::CreateListEditor(wxWindow* parent,
                                          const FieldInfo& field,
                                          const mtext::Variant& current,
                                          WriteVariantFn write_fn) {
  auto items = std::make_shared<mtext::Array>();
  if (auto* arr = current.Get<mtext::Array>()) *items = *arr;

  auto* panel = new wxPanel(parent, wxID_ANY);
  auto* sizer = new wxBoxSizer(wxVERTICAL);

  auto* top = new wxBoxSizer(wxHORIZONTAL);
  auto* count_lbl = new wxStaticText(
      panel, wxID_ANY,
      wxString::Format("%zu item%s", items->size(),
                       items->size() == 1 ? "" : "s"));
  count_lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  top->Add(count_lbl, 1, wxALIGN_CENTER_VERTICAL);

  auto* add_btn = new wxButton(panel, wxID_ANY, "+", wxDefaultPosition,
                               wxSize(24, 22), wxBU_EXACTFIT);
  add_btn->Bind(wxEVT_BUTTON, [this, items, field, write_fn](wxCommandEvent&) {
    mtext::Variant item;
    switch (field.element_type) {
      case FieldType::kInt:
        item = 0;
        break;
      case FieldType::kFloat:
        item = 0.0;
        break;
      case FieldType::kNodeRef:
        item = mtext::NodePath{};
        break;
      case FieldType::kAsset:
        item = mtext::Resource{};
        break;
      default:
        item = std::string();
        break;
    }
    items->push_back(std::move(item));
    write_fn(mtext::Variant(*items), false);
    ScheduleRebuild();
  });
  top->Add(add_btn, 0);
  sizer->Add(top, 0, wxEXPAND | wxBOTTOM, 4);

  for (size_t i = 0; i < items->size(); ++i) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* rem_btn = new wxButton(panel, wxID_ANY, "x", wxDefaultPosition,
                                 wxSize(22, 22), wxBU_EXACTFIT);
    rem_btn->Bind(wxEVT_BUTTON, [this, items, i, write_fn](wxCommandEvent&) {
      if (i < items->size()) {
        items->erase(items->begin() + static_cast<ptrdiff_t>(i));
        write_fn(mtext::Variant(*items), false);
        ScheduleRebuild();
      }
    });
    row->Add(rem_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    wxWindow* elem_editor = CreateValueEditor(
        panel, field, field.element_type, (*items)[i],
        [items, i, write_fn](const mtext::Variant& val, bool coalesced) {
          if (i < items->size()) {
            (*items)[i] = val;
            write_fn(mtext::Variant(*items), coalesced);
          }
        });
    row->Add(elem_editor, 1, wxEXPAND);
    sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 3);
  }

  panel->SetSizer(sizer);
  return panel;
}

wxPanel* InspectorPanel::CreateEventCallsEditor(wxWindow* parent,
                                                const mtext::Variant& current,
                                                WriteVariantFn write_fn) {
  auto calls = std::make_shared<std::vector<EventCall>>(
      Event::FromVariant(current).value_or(std::vector<EventCall>{}));

  auto* panel = new wxPanel(parent, wxID_ANY);
  auto* sizer = new wxBoxSizer(wxVERTICAL);

  auto* top = new wxBoxSizer(wxHORIZONTAL);
  auto* count_lbl = new wxStaticText(
      panel, wxID_ANY,
      wxString::Format("%zu call%s", calls->size(),
                       calls->size() == 1 ? "" : "s"));
  count_lbl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
  top->Add(count_lbl, 1, wxALIGN_CENTER_VERTICAL);

  auto* add_btn = new wxButton(panel, wxID_ANY, "+", wxDefaultPosition,
                               wxSize(24, 22), wxBU_EXACTFIT);
  add_btn->Bind(wxEVT_BUTTON, [this, calls, write_fn](wxCommandEvent&) {
    calls->emplace_back();
    write_fn(Event::ToVariant(*calls), false);
    ScheduleRebuild();
  });
  top->Add(add_btn, 0);
  sizer->Add(top, 0, wxEXPAND | wxBOTTOM, 4);

  const auto& registry = TypeRegistry::Instance();

  for (size_t i = 0; i < calls->size(); ++i) {
    auto* box = new wxPanel(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxBORDER_SIMPLE);
    auto* bs = new wxBoxSizer(wxVERTICAL);

    auto* line1 = new wxBoxSizer(wxHORIZONTAL);
    auto* rem_btn = new wxButton(box, wxID_ANY, "x", wxDefaultPosition,
                                 wxSize(22, 22), wxBU_EXACTFIT);
    rem_btn->Bind(wxEVT_BUTTON, [this, calls, i, write_fn](wxCommandEvent&) {
      if (i < calls->size()) {
        calls->erase(calls->begin() + static_cast<ptrdiff_t>(i));
        write_fn(Event::ToVariant(*calls), false);
        ScheduleRebuild();
      }
    });
    line1->Add(rem_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    wxWindow* node_picker = CreateNodePicker(
        box, (*calls)[i].target.path,
        [this, calls, i, write_fn](const std::string& p) {
          if (i < calls->size()) {
            (*calls)[i].target.path = p;
            write_fn(Event::ToVariant(*calls), false);
            ScheduleRebuild();
          }
        });
    line1->Add(node_picker, 1, wxEXPAND);
    bs->Add(line1, 0, wxEXPAND | wxBOTTOM, 3);

    // Build method choices: GameObject.SetActive(bool) + target node's script methods
    struct MethodOption {
      std::string component;
      MethodDesc method;
    };
    auto options = std::make_shared<std::vector<MethodOption>>();
    wxArrayString method_labels;
    options->push_back({"GameObject", {"SetActive", "bool"}});
    method_labels.Add("GameObject.SetActive (bool)");

    if (const MapNode* target =
            scene_.GetMap().FindNode((*calls)[i].target.path)) {
      for (const auto& comp : target->components) {
        const ComponentInfo* cinfo = registry.FindComponent(comp.type);
        if (!cinfo || cinfo->methods.empty()) continue;
        for (const auto& m : cinfo->methods) {
          options->push_back({cinfo->name, m});
          method_labels.Add(wxString::FromUTF8(
              cinfo->name + "." + m.name +
              (m.arg.empty() ? "" : " (" + m.arg + ")")));
        }
      }
    }

    int m_sel = wxNOT_FOUND;
    for (size_t opt_i = 0; opt_i < options->size(); ++opt_i) {
      if ((*options)[opt_i].component == (*calls)[i].component &&
          (*options)[opt_i].method.name == (*calls)[i].method) {
        m_sel = static_cast<int>(opt_i);
        break;
      }
    }
    if (m_sel == wxNOT_FOUND && !(*calls)[i].component.empty()) {
      m_sel = static_cast<int>(options->size());
      options->push_back({(*calls)[i].component, {(*calls)[i].method, ""}});
      method_labels.Add(wxString::FromUTF8((*calls)[i].component + "." +
                                           (*calls)[i].method));
    }

    auto* method_choice = new wxChoice(box, wxID_ANY, wxDefaultPosition,
                                         wxDefaultSize, method_labels);
    if (m_sel != wxNOT_FOUND) method_choice->SetSelection(m_sel);
    method_choice->Bind(
        wxEVT_CHOICE, [this, method_choice, options, calls, i,
                       write_fn](wxCommandEvent&) {
          const int idx = method_choice->GetSelection();
          if (i >= calls->size() || idx < 0 ||
              idx >= static_cast<int>(options->size())) {
            return;
          }
          auto& call = (*calls)[i];
          const auto& opt = (*options)[idx];
          call.component = opt.component;
          call.method = opt.method.name;
          if (opt.method.arg == "bool") call.argument = false;
          else if (opt.method.arg == "int") call.argument = 0;
          else if (opt.method.arg == "float") call.argument = 0.0;
          else if (opt.method.arg == "string") call.argument = std::string();
          else call.argument = mtext::Variant();
          write_fn(Event::ToVariant(*calls), false);
          ScheduleRebuild();
        });
    bs->Add(method_choice, 0, wxEXPAND | wxBOTTOM, 3);

    using T = mtext::Variant::Type;
    FieldInfo arg_field;
    wxWindow* arg_editor = nullptr;
    auto arg_write = [calls, i, write_fn](const mtext::Variant& v, bool coalesced) {
      if (i < calls->size()) {
        (*calls)[i].argument = v;
        write_fn(Event::ToVariant(*calls), coalesced);
      }
    };
    switch ((*calls)[i].argument.GetType()) {
      case T::kBool:
        arg_editor = CreateValueEditor(box, arg_field, FieldType::kBool,
                                       (*calls)[i].argument, arg_write);
        break;
      case T::kInt:
        arg_editor = CreateValueEditor(box, arg_field, FieldType::kInt,
                                       (*calls)[i].argument, arg_write);
        break;
      case T::kFloat:
        arg_editor = CreateValueEditor(box, arg_field, FieldType::kFloat,
                                       (*calls)[i].argument, arg_write);
        break;
      case T::kString:
        arg_editor = CreateValueEditor(box, arg_field, FieldType::kString,
                                       (*calls)[i].argument, arg_write);
        break;
      default:
        break;
    }
    if (arg_editor) bs->Add(arg_editor, 0, wxEXPAND);

    auto* wrap = new wxBoxSizer(wxVERTICAL);
    wrap->Add(bs, 0, wxEXPAND | wxALL, 4);
    box->SetSizer(wrap);
    sizer->Add(box, 0, wxEXPAND | wxBOTTOM, 4);
  }

  panel->SetSizer(sizer);
  return panel;
}

void InspectorPanel::AddLockGroupRow(
    wxWindow* parent, wxBoxSizer* parent_sizer, const char* label_text,
    const std::string& prefix, mtext::PropertyList& props,
    std::function<void(bool coalesced)> on_prop_changed) {
  const std::string key_x = prefix + "_x";
  const std::string key_y = prefix + "_y";
  const std::string key_z = prefix + "_z";

  bool x = false;
  bool y = false;
  bool z = false;
  if (!props.Get(key_x) && !props.Get(key_y) && !props.Get(key_z)) {
    if (const auto* val = props.Get("lock_transform_x")) x = val->ToBool().value_or(false);
    if (const auto* val = props.Get("lock_transform_y")) y = val->ToBool().value_or(false);
    if (const auto* val = props.Get("lock_transform_z")) z = val->ToBool().value_or(false);
    if (const auto* val = props.Get("lock_position_x")) x = val->ToBool().value_or(false);
    if (const auto* val = props.Get("lock_position_y")) y = val->ToBool().value_or(false);
    if (const auto* val = props.Get("lock_position_z")) z = val->ToBool().value_or(false);

    if (!x && !y && !z) {
      if (const auto* dofs = props.Get("allowed_dofs")) {
        std::string mode = dofs->ToString().value_or("");
        if (prefix == "lock_translation") {
          if (mode == "translation_x") y = z = true;
          else if (mode == "translation_y") x = z = true;
          else if (mode == "translation_xy" || mode == "plane_2d") z = true;
        } else if (prefix == "lock_rotation") {
          if (mode == "translation_xyz" || mode == "translation_xy" ||
              mode == "translation_x" || mode == "translation_y") {
            x = y = z = true;
          } else if (mode == "plane_2d") {
            x = y = true;
          }
        }
      }
    }
  }
  if (const auto* val = props.Get(key_x)) x = val->ToBool().value_or(false);
  if (const auto* val = props.Get(key_y)) y = val->ToBool().value_or(false);
  if (const auto* val = props.Get(key_z)) z = val->ToBool().value_or(false);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* label = new wxStaticText(parent, wxID_ANY, label_text,
                                 wxDefaultPosition, wxSize(118, -1),
                                 wxST_ELLIPSIZE_END);
  const bool is_rotation =
      std::string(label_text).find("Rotation") != std::string::npos;
  label->SetToolTip(is_rotation
                        ? "Freeze rotation around the world X, Y, or Z axes"
                        : "Freeze position along the world X, Y, or Z axes");
  row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);

  auto* cb_x = new wxCheckBox(parent, wxID_ANY, "X");
  auto* cb_y = new wxCheckBox(parent, wxID_ANY, "Y");
  auto* cb_z = new wxCheckBox(parent, wxID_ANY, "Z");
  cb_x->SetValue(x);
  cb_y->SetValue(y);
  cb_z->SetValue(z);
  cb_x->SetForegroundColour(Colors::kAxisX);
  cb_y->SetForegroundColour(Colors::kAxisY);
  cb_z->SetForegroundColour(Colors::kAxisZ);

  auto update_label = [label, parent, cb_x, cb_y, cb_z]() {
    const bool any =
        cb_x->GetValue() || cb_y->GetValue() || cb_z->GetValue();
    label->SetFont(any ? parent->GetFont().Bold() : parent->GetFont());
    label->Refresh();
  };
  update_label();

  auto sync = [this, &props, key_x, key_y, key_z, cb_x, cb_y, cb_z,
               update_label, on_prop_changed]() {
    auto apply_one = [&](const std::string& key, bool v) {
      if (v) props.Set(key, true);
      else props.Remove(key);
    };
    apply_one(key_x, cb_x->GetValue());
    apply_one(key_y, cb_y->GetValue());
    apply_one(key_z, cb_z->GetValue());
    props.Remove("allowed_dofs");
    update_label();
    on_prop_changed(false);
  };

  cb_x->Bind(wxEVT_CHECKBOX, [sync](wxCommandEvent&) { sync(); });
  cb_y->Bind(wxEVT_CHECKBOX, [sync](wxCommandEvent&) { sync(); });
  cb_z->Bind(wxEVT_CHECKBOX, [sync](wxCommandEvent&) { sync(); });

  label->Bind(wxEVT_RIGHT_DOWN,
              [this, label, &props, key_x, key_y, key_z, cb_x, cb_y, cb_z,
               update_label, on_prop_changed](wxMouseEvent&) {
                wxMenu menu;
                constexpr int kIdReset = 1;
                const bool any =
                    cb_x->GetValue() || cb_y->GetValue() || cb_z->GetValue();
                menu.Append(kIdReset, "Reset to Default")->Enable(any);
                menu.Bind(wxEVT_MENU, [&](wxCommandEvent&) {
                  props.Remove(key_x);
                  props.Remove(key_y);
                  props.Remove(key_z);
                  props.Remove("allowed_dofs");
                  cb_x->SetValue(false);
                  cb_y->SetValue(false);
                  cb_z->SetValue(false);
                  update_label();
                  on_prop_changed(false);
                });
                label->PopupMenu(&menu);
              });

  row->Add(cb_x, 0, wxRIGHT, 8);
  row->Add(cb_y, 0, wxRIGHT, 8);
  row->Add(cb_z, 0);
  parent_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 5);
}

void InspectorPanel::AddRawPropertyRow(
    wxWindow* parent, wxBoxSizer* parent_sizer, const mtext::Property& prop,
    std::function<void(const std::string& new_val_text)> on_commit,
    std::function<void()> on_remove) {
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  auto* label = new wxStaticText(parent, wxID_ANY, wxString::FromUTF8(prop.key),
                                 wxDefaultPosition, wxSize(118, -1),
                                 wxST_ELLIPSIZE_END);
  label->SetFont(label->GetFont().Italic());
  label->SetToolTip(
      "Not a field of this component. Edit the raw value or right-click to "
      "remove it.");
  label->Bind(wxEVT_RIGHT_DOWN, [label, on_remove](wxMouseEvent&) {
    wxMenu menu;
    menu.Append(1, "Remove Property");
    menu.Bind(wxEVT_MENU, [on_remove](wxCommandEvent&) { on_remove(); });
    label->PopupMenu(&menu);
  });
  row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);

  auto* ctrl = new wxTextCtrl(
      parent, wxID_ANY, wxString::FromUTF8(mtext::WriteValue(prop.value)),
      wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
  ctrl->Bind(wxEVT_TEXT_ENTER, [ctrl, on_commit](wxCommandEvent&) {
    on_commit(ctrl->GetValue().ToStdString());
  });
  row->Add(ctrl, 1, wxEXPAND);
  parent_sizer->Add(row, 0, wxEXPAND | wxBOTTOM, 5);
}

}  // namespace Mercury::EditorUI
