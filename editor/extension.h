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
#include <wx/wx.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
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
  // The custom inspector for a component type, or null.
  const EditorInspectorInfo* FindInspector(std::string_view component) const;
  size_t Count() const;

 private:
  EditorExtensionRegistry() = default;

  std::vector<PanelEntry> panels_;
  std::vector<MenuItemEntry> menu_items_;
  std::vector<InspectorEntry> inspectors_;
  std::vector<ProjectMenuItemEntry> project_menu_items_;
  std::vector<ViewportToolEntry> viewport_tools_;
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
class EditorContext {
 public:
  // Created by EditorExtensionLoader; public so a host or a test can build a
  // context without going through a module.
  EditorContext(std::string module_id, EditorFrame* frame, EditorScene* scene,
                EditorSettings* settings, Console* console,
                wxAuiManager* aui_manager);
  ~EditorContext();
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
 * @brief The table a module exports under `mercury_editor_extension_v1`
 *
 * Returned by the entry point MERCURY_EDITOR_EXTENSION defines. The loader
 * checks the ABI and engine version before calling `create`.
 */
struct EditorExtensionEntry {
  uint32_t abi_version = kEditorExtensionAbiVersion;
  const char* engine_version = MERCURY_ENGINE_VERSION;
  EditorExtension* (*create)() = nullptr;
  void (*destroy)(EditorExtension*) = nullptr;
};

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
