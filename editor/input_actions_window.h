/*
 * input_actions_window.h
 *
 * The Input Actions window, modeled on Unity's Input Actions editor: edit a
 * .minput file (input/input_actions.h) in three columns.
 *
 *   Action Maps   groups of actions (Player, UI...), enabled together
 *   Actions       the selected map's actions, each with its bindings and
 *                 2D vector composites
 *   Properties    the selection: an action's type, or a binding's control
 *                 path (pick it from a list, or Listen and press it)
 *
 * Saving also writes the C++ header named in the file (Code menu), so
 * scripts use GameInput.Player.Jump instead of Find("Player/Jump").
 */
#ifndef MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H
#define MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "console.h"
#include "input/input_actions.h"
#include "input/input_manager.h"
#include "project/project.h"

namespace Mercury {

class InputActionsWindow {
 public:
  static constexpr const char* kWindowName = "Input Actions";

  explicit InputActionsWindow(Console& console);

  void SetProject(std::shared_ptr<Project> project);

  // Opens a .minput file (res:// path); false if it can't be read.
  bool Open(const std::string& res_path);
  void Close();
  bool IsOpen() const { return !path_.empty(); }
  const std::string& path() const { return path_; }
  bool IsDirty() const { return dirty_; }
  // Writes the file, and the generated header if it has one.
  bool Save();

  void Undo();
  void Redo();
  // True while the window has focus, so Ctrl+S / Ctrl+Z go to it.
  bool HasFocus() const { return has_focus_; }

  // Draws the window; `open` is cleared when it is closed. `input` is read
  // while a binding is listening for a control.
  void Draw(const InputManager* input, bool* open);

 private:
  struct Selection {
    enum class Kind { kNone, kAction, kBinding, kComposite };
    Kind kind = Kind::kNone;
    int action = -1;  // index into desc_.actions
    int index = -1;   // binding or composite index
    bool operator==(const Selection&) const = default;
  };

  // Call before a change, so it can be undone.
  void Checkpoint();
  void MarkChanged();
  const std::string& SelectedMap() const;
  std::vector<int> ActionsOf(const std::string& map) const;
  std::string UniqueActionName(const std::string& map, std::string base) const;
  std::string UniqueMapName(std::string base) const;
  void AddMap();
  void RenameMap(const std::string& from, const std::string& to);
  void DeleteMap(const std::string& map);
  void AddAction(const std::string& map);
  void DeleteSelected();
  void DuplicateSelected();
  bool WriteHeader();
  void CaptureListen(const InputManager* input);

  void DrawToolbar();
  void DrawMaps();
  void DrawActions();
  void DrawActionContextMenu(int action);
  void DrawProperties();
  void DrawActionProperties(InputActionDesc& action);
  // A control path field with a picker and a Listen button; true if changed.
  bool DrawControlPath(const char* id, std::string& path);
  bool DrawPathList(const char* label, std::vector<std::string>& paths);

  Console& console_;
  std::shared_ptr<Project> project_;
  bool has_focus_ = false;

  std::string path_;
  InputActionsDesc desc_;
  bool dirty_ = false;
  int map_ = 0;  // index into desc_.maps
  Selection selection_;
  // Map being renamed in place, and its new name.
  int renaming_map_ = -1;
  std::string rename_buffer_;
  bool focus_rename_ = false;
  // The action's name when its Name field was clicked, restored if cleared.
  std::string name_before_edit_;
  // Header problems from the last save (name collisions).
  std::vector<std::string> header_errors_;

  struct Snapshot {
    InputActionsDesc desc;
    int map = 0;
    Selection selection;
  };
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;

  // Listen: the ImGui id of the field waiting for a control, the gamepad
  // buttons held when it started, and what was pressed.
  unsigned listen_id_ = 0;
  bool listen_pad_[15] = {};
  std::optional<std::string> listened_;
  char picker_filter_[64] = "";
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H
