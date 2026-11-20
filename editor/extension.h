// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * extension.h
 *
 * The editor-extension API. A project's `system/` folder is built into a
 * dynamic library the editor loads while the project is open; the library
 * derives from EditorExtension, registers panels, menus and inspectors through
 * EditorContext, and is unloaded when the project closes or the user asks to
 * reload.
 *
 *   class MyTool : public Mercury::EditorUI::EditorExtension {
 *    public:
 *     void OnLoad(EditorContext& context) override {
 *       context.AddPanel({.id = "my.tool", .title = "My Tool",
 *                         .create = [](wxWindow* parent) {
 *                           return new MyToolPanel(parent);
 *                         }});
 *     }
 *   };
 *
 *   MERCURY_EDITOR_EXTENSION(MyTool);
 *
 * Everything registered through the context belongs to the module and is
 * removed when it unloads, so a reload never leaves a callback pointing into a
 * library that is no longer mapped. Extensions run on the editor's UI thread
 * and get the real wxWidgets objects: there is no wrapper layer.
 */
#ifndef MERCURY_EDITOR_EXTENSION_H
#define MERCURY_EDITOR_EXTENSION_H

#include <wx/aui/aui.h>
#include <wx/weakref.h>
#include <wx/wx.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "globals.h"
#include "overlay_canvas.h"
#include "project/asset_importer.h"

namespace Mercury {

class Console;
class EditorSettings;
class EditorScene;
struct MapNode;

}  // namespace Mercury

namespace Mercury::EditorUI {

class EditorFrame;
class EditorContext;

// The extension ABI. Bump it when EditorExtension, EditorContext or any
// structure passed across the boundary changes; a module built for another
// version is refused with a logged error instead of crashing.
inline constexpr uint32_t kEditorExtensionAbiVersion = 1;

/**
 * @brief A dockable panel an extension adds to the editor
 */
struct EditorPanelInfo {
  // Unique within the module; also the wxAui pane name.
  std::string id;
  // The pane caption and Window-menu label.
  std::string title;
  // Factory for the panel content. The editor owns and destroys it.
  std::function<wxWindow*(wxWindow* parent)> create;
};

/**
 * @brief A menu item an extension adds to a top-level menu
 */
struct EditorMenuItemInfo {
  // Top-level menu to append to (created when missing). "&" is ignored and the
  // comparison is case-insensitive.
  std::string menu = "Extensions";
  std::string label;
  // Optional accelerator, e.g. "Ctrl+Shift+H".
  std::string accelerator;
  std::function<void()> callback;
};

/**
 * @brief A custom inspector for one component type
 *
 * When an inspector is registered for a component, its window replaces the
 * generic field rows for that component in the Inspector.
 */
struct EditorInspectorInfo {
  // Component type name, e.g. "Transform" or a game component.
  std::string component;
  std::function<wxWindow*(wxWindow* parent, EditorScene& scene, MapNode& node)>
      create;
};

/**
 * @brief An entry in the Project Browser's folder/file context menu
 */
struct EditorProjectMenuItemInfo {
  std::string label;
  // res:// path of the clicked file/folder and whether it is a folder.
  std::function<void(const std::string& res_path, bool is_directory)> callback;
};

/**
 * @brief A mouse event routed to viewport tools (canvas pixels)
 */
struct EditorViewportMouseEvent {
  enum class Action { kDown, kUp, kMove, kWheel };
  Action action = Action::kMove;
  glm::vec2 position = glm::vec2(0.0f);
  int button = 0;  // wxMouseEvent::GetButton(); 0 for move
  float wheel_steps = 0.0f;
  int modifiers = 0;  // wxMOD_* bits
};

/**
 * @brief A key event routed to viewport tools
 */
struct EditorViewportKeyEvent {
  int key_code = 0;  // wxKeyEvent::GetKeyCode()
  bool down = false;
  int modifiers = 0;  // wxMOD_* bits
};

/**
 * @brief A Scene viewport tool: optional input handling plus an overlay
 *
 * Mouse and key callbacks run before the editor's own gizmo and camera code;
 * returning true consumes the event. The draw callback runs after the editor's
 * own overlays, in viewport pixels.
 */
struct EditorViewportToolInfo {
  std::string id;
  std::function<bool(const EditorViewportMouseEvent&, EditorScene&)> on_mouse;
  std::function<bool(const EditorViewportKeyEvent&, EditorScene&)> on_key;
  std::function<void(OverlayCanvas&, EditorScene&, glm::vec2 mouse)> draw;
};

/**
 * @brief The editor's extension registry, filled through EditorContext
 *
 * Entries record the module that registered them so unloading a module can
 * remove exactly its entries.
 */
class EditorExtensionRegistry {
 public:
  struct PanelEntry {
    std::string module;
    EditorPanelInfo info;
  };
  struct MenuItemEntry {
    std::string module;
    EditorMenuItemInfo info;
  };
  struct InspectorEntry {
    std::string module;
    EditorInspectorInfo info;
  };
  struct ProjectMenuItemEntry {
    std::string module;
    EditorProjectMenuItemInfo info;
  };
  struct ViewportToolEntry {
    std::string module;
    EditorViewportToolInfo info;
  };

  static EditorExtensionRegistry& Instance();

  void AddPanel(std::string module, EditorPanelInfo info);
  void AddMenuItem(std::string module, EditorMenuItemInfo info);
  void AddInspector(std::string module, EditorInspectorInfo info);
  void AddProjectMenuItem(std::string module, EditorProjectMenuItemInfo info);
  void AddViewportTool(std::string module, EditorViewportToolInfo info);

  // Removes every entry registered by `module`.
  void RemoveModule(std::string_view module);
  // Removes every entry (tests, and closing the editor).
  void Clear();

  const std::vector<PanelEntry>& Panels() const { return panels_; }
  const std::vector<MenuItemEntry>& MenuItems() const { return menu_items_; }
  const std::vector<ProjectMenuItemEntry>& ProjectMenuItems() const {
    return project_menu_items_;
  }
  const std::vector<ViewportToolEntry>& ViewportTools() const {
    return viewport_tools_;
  }
  // A copy of the custom inspector for a component type, if there is one. The
  // copy also carries the module id for failure reporting.
  std::optional<InspectorEntry> CopyInspector(std::string_view component) const;
  size_t Count() const;

  // Safe iteration: the entries (and the std::functions in them) are copied
  // first, so a callback may register or unregister without invalidating the
  // loop.
  void ForEachPanel(const std::function<void(const PanelEntry&)>& fn) const;
  void ForEachMenuItem(
      const std::function<void(const MenuItemEntry&)>& fn) const;
  void ForEachProjectMenuItem(
      const std::function<void(const ProjectMenuItemEntry&)>& fn) const;
  void ForEachViewportTool(
      const std::function<void(const ViewportToolEntry&)>& fn) const;

  // --- callback safety ----------------------------------------------------
  // A callback runs while the depth is > 0. The loader defers Reload/UnloadAll
  // while this is true, so a callback (or a modal dialog it opens) cannot
  // unload the module whose code is running.
  void BeginCallback();
  void EndCallback();
  bool InCallback() const { return callback_depth_ > 0; }

  // Module failures are reported here (the loader points it at the Console).
  using ErrorSink =
      std::function<void(std::string_view module, std::string_view message)>;
  void SetErrorSink(ErrorSink sink);
  void ReportFailure(std::string_view module, const char* what,
                     std::string_view detail);
  // After too many failures a module is no longer called.
  bool IsModuleFaulted(std::string_view module) const;

 private:
  EditorExtensionRegistry() = default;

  std::vector<PanelEntry> panels_;
  std::vector<MenuItemEntry> menu_items_;
  std::vector<InspectorEntry> inspectors_;
  std::vector<ProjectMenuItemEntry> project_menu_items_;
  std::vector<ViewportToolEntry> viewport_tools_;
  ErrorSink error_sink_;
  std::unordered_map<std::string, int> failures_;
  int callback_depth_ = 0;
};

/**
 * @brief A loaded editor extension
 *
 * The module subclasses it and registers its UI in OnLoad. Every callback runs
 * on the UI thread. A callback never runs after OnUnload returns.
 */
class EditorExtension {
 public:
  virtual ~EditorExtension() = default;

  // Register panels, menus and inspectors here.
  virtual void OnLoad(EditorContext& context) = 0;
  // The last call into the module before its UI is destroyed and the library
  // is unloaded. Release non-wx resources here; the editor owns the wx objects.
  virtual void OnUnload() {}
  // The project this module was loaded for is open (called after OnLoad).
  virtual void OnProjectOpened(EditorScene& scene) { (void)scene; }
  // The project is closing; registered UI still exists for this call.
  virtual void OnProjectClosed() {}
  // The scene selection changed (node, asset or nothing).
  virtual void OnSelectionChanged(EditorScene& scene) { (void)scene; }
  // Play started, after the build and before the game process runs.
  virtual void OnPlayStarted() {}
  // Play stopped.
  virtual void OnPlayStopped() {}
};

/**
 * @brief What an extension uses to reach the editor and register UI
 *
 * One context exists per loaded module. It returns the real editor objects
 * (no wrappers); registrations are owned by the module and removed when the
 * context is destroyed during unload.
 */
class EditorContext : public wxEvtHandler {
 public:
  // Created by EditorExtensionLoader; public so a host or a test can build a
  // context without going through a module.
  EditorContext(std::string module_id, EditorFrame* frame, EditorScene* scene,
                EditorSettings* settings, Console* console,
                wxAuiManager* aui_manager);
  ~EditorContext() override;
  EditorContext(const EditorContext&) = delete;
  EditorContext& operator=(const EditorContext&) = delete;

  const std::string& GetModuleId() const { return module_id_; }
  EditorFrame* GetFrame() const { return frame_; }
  EditorScene* GetScene() const { return scene_; }
  wxAuiManager& GetAuiManager();
  EditorSettings& GetSettings();
  Console& GetConsole();

  // Registers a dockable panel and its Window-menu entry.
  void AddPanel(EditorPanelInfo info);
  // Registers a menu item on a top-level menu.
  void AddMenuItem(EditorMenuItemInfo info);
  // Registers a custom Inspector for a component type.
  void AddInspector(EditorInspectorInfo info);
  // Registers an entry in the Project Browser's context menus.
  void AddProjectMenuItem(EditorProjectMenuItemInfo info);
  // Registers a Scene viewport tool: input callbacks plus an overlay.
  void AddViewportTool(EditorViewportToolInfo info);
  // Registers an asset importer owned by this module. Unloading the module
  // unregisters exactly its importers and restores the earlier mapping.
  void AddAssetImporter(AssetImporterInfo info);

  /**
   * @brief Binds an event handler that is unbound when the module unloads
   *
   * Never call `handler.Bind(...)` directly on an editor-owned wx object: the
   * lambda would point into the module after its library is unmapped. Use this
   * method for editor- and module-owned handlers alike; the binding is
   * removed by UnbindAll (the loader calls it before destroying the module's
   * windows) and by the context destructor.
   * @param id the event/menu id, or wxID_ANY for a generated one
   */
  template <typename EventTag>
  void Bind(wxEvtHandler& handler, const EventTag& event_type,
            std::function<void(wxEvent&)> callback, int id = wxID_ANY) {
    if (id == wxID_ANY) id = wxWindow::NewControlId();
    bound_callbacks_[id] = std::move(callback);
    handler.Bind(event_type, &EditorContext::OnBoundEvent, this, id);
    // A weak reference keeps UnbindAll safe if the module destroyed its own
    // handler before unloading.
    wxWeakRef<wxEvtHandler> weak(&handler);
    bound_unbinders_.push_back([weak, event_type, id, this]() mutable {
      if (wxEvtHandler* target = weak.get()) {
        target->Unbind(event_type, &EditorContext::OnBoundEvent, this, id);
      }
    });
  }

  // Removes every Bind() before the module's wx objects are destroyed. Safe to
  // call more than once.
  void UnbindAll();

 private:
  friend class EditorExtensionLoader;

  EditorExtensionRegistry* registry_ = nullptr;
  std::string module_id_;
  EditorFrame* frame_ = nullptr;
  EditorScene* scene_ = nullptr;
  EditorSettings* settings_ = nullptr;
  Console* console_ = nullptr;
  wxAuiManager* aui_manager_ = nullptr;
  // Windows the module created itself (top-level frames, dialogs). The loader
  // destroys them before the library is unloaded.
  std::vector<wxWindow*> tracked_windows_;

 public:
  // A module must call this for every top-level window (wxFrame/wxDialog) it
  // creates outside its registered panels, so it is destroyed while the
  // module's code is still mapped.
  void TrackWindow(wxWindow* window);

  // The windows TrackWindow recorded, for EditorExtensionLoader.
  const std::vector<wxWindow*>& GetTrackedWindows() const {
    return tracked_windows_;
  }

 private:
  void OnBoundEvent(wxEvent& event);

  std::vector<std::function<void()>> bound_unbinders_;
  std::unordered_map<int, std::function<void(wxEvent&)>> bound_callbacks_;
};

/**
 * @brief Whether a module built for `module_version` can run on this engine
 *
 * Compares major.minor only, so a patch release or a `-dev`/`-rc` suffix does
 * not reject modules. Unparseable versions must match exactly.
 */
bool IsExtensionVersionCompatible(std::string_view module_version,
                                  std::string_view engine_version);

/**
 * @brief The table a module exports under `mercury_editor_extension_v1` *
 * Returned by the entry point MERCURY_EDITOR_EXTENSION defines. The loader
 * checks the ABI and engine version before calling `create`.
 */
struct EditorExtensionEntry {
  uint32_t abi_version = kEditorExtensionAbiVersion;
  const char* engine_version = MERCURY_ENGINE_VERSION;
  EditorExtension* (*create)() = nullptr;
  void (*destroy)(EditorExtension*) = nullptr;
};

/**
 * @brief A reusable pool of menu ids for extensions
 *
 * Release returns the slot for reuse, so repeated reloads never exhaust the
 * block the editor reserves.
 */
class EditorMenuIdPool {
 public:
  explicit EditorMenuIdPool(int capacity = 1000) : capacity_(capacity) {}

  // A free slot in [0, capacity), or -1 when the pool is exhausted.
  int Acquire();
  void Release(int slot);
  void Reset();

  int GetCapacity() const { return capacity_; }

 private:
  int capacity_;
  int next_ = 0;
  std::vector<int> free_;
};

// Runs a module callback with the re-entrancy guard and exception boundary.
template <typename Fn>
void RunExtensionCallback(std::string_view module, const char* what, Fn&& fn) {
  EditorExtensionRegistry& registry = EditorExtensionRegistry::Instance();
  if (registry.IsModuleFaulted(module)) return;
  registry.BeginCallback();
  struct Scope {
    EditorExtensionRegistry& registry;
    ~Scope() { registry.EndCallback(); }
  } scope{registry};
  try {
    fn();
  } catch (const std::exception& e) {
    registry.ReportFailure(module, what, e.what());
  } catch (...) {
    registry.ReportFailure(module, what, "unknown exception");
  }
}

// The same for a callback with a return value; `fallback` is used when the
// module is faulted or throws.
template <typename Fn, typename Fallback>
auto RunExtensionCallback(std::string_view module, const char* what, Fn&& fn,
                          Fallback&& fallback) -> decltype(fn()) {
  EditorExtensionRegistry& registry = EditorExtensionRegistry::Instance();
  if (registry.IsModuleFaulted(module)) return fallback();
  registry.BeginCallback();
  struct Scope {
    EditorExtensionRegistry& registry;
    ~Scope() { registry.EndCallback(); }
  } scope{registry};
  try {
    return fn();
  } catch (const std::exception& e) {
    registry.ReportFailure(module, what, e.what());
  } catch (...) {
    registry.ReportFailure(module, what, "unknown exception");
  }
  return fallback();
}

}  // namespace Mercury::EditorUI

/**
 * @brief Export a module's extension entry point
 * @param Type A class deriving from Mercury::EditorUI::EditorExtension
 *
 * Put this in one .cc file of the `system/` module. The editor looks up
 * `mercury_editor_extension_v1` with dlsym/GetProcAddress.
 */
#define MERCURY_EDITOR_EXTENSION(Type)                                        \
  extern "C" const ::Mercury::EditorUI::EditorExtensionEntry*                 \
  mercury_editor_extension_v1() {                                             \
    static_assert(                                                            \
        std::is_base_of_v<::Mercury::EditorUI::EditorExtension, Type>,        \
        "MERCURY_EDITOR_EXTENSION types must derive from EditorExtension");   \
    static const ::Mercury::EditorUI::EditorExtensionEntry entry{             \
        ::Mercury::EditorUI::kEditorExtensionAbiVersion,                      \
        MERCURY_ENGINE_VERSION,                                               \
        []() -> ::Mercury::EditorUI::EditorExtension* { return new Type(); }, \
        [](::Mercury::EditorUI::EditorExtension* extension) {                 \
          delete extension;                                                   \
        }};                                                                   \
    return &entry;                                                            \
  }

#endif  // MERCURY_EDITOR_EXTENSION_H
