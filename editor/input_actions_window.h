/*
 * input_actions_window.h
 *
 * Controller for editing a .minput file (input/input_actions.h):
 *
 *   Action Maps   groups of actions (Player, UI...), enabled together
 *   Actions       the selected map's actions, each with its bindings and
 *                 2D vector composites
 *   Properties    the selection: an action's type, or a binding's control
 *                 path (pick it from a list, or Listen and press it)
 *
 * Saving also writes the AngelScript namespace named in the file so scripts
 * use GameInput.Player.Jump instead of Find("Player/Jump").
 */
#ifndef MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H
#define MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H

#include <cstdint>
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
  struct Selection {
    enum class Kind { kNone, kAction, kBinding, kComposite };
    Kind kind = Kind::kNone;
    int action = -1;  // index into desc_.actions
    int index = -1;   // binding or composite index
    bool operator==(const Selection&) const = default;
  };

  explicit InputActionsWindow(Console& console);

  void SetProject(std::shared_ptr<Project> project);

  bool Open(const std::string& res_path);
  void Close();
  bool IsOpen() const { return !path_.empty(); }
  const std::string& path() const { return path_; }
  bool IsDirty() const { return dirty_; }
  bool Save();
  bool WriteHeader();

  bool IsProjectActions() const;
  void SetAsProjectActions();

  void Undo();
  void Redo();
  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }

  void BeginEdit();
  void CommitEdit();

  uint64_t StateRevision() const { return state_revision_; }

  const InputActionsDesc& Desc() const { return desc_; }
  int SelectedMapIndex() const { return map_; }
  const std::string& SelectedMap() const;
  void SelectMap(int index);

  const Selection& CurrentSelection() const { return selection_; }
  void Select(Selection selection);

  std::vector<int> ActionsOf(const std::string& map) const;
  const std::vector<std::string>& HeaderErrors() const { return header_errors_; }
  bool IsActionNameTaken(int action_idx, const std::string& name) const;
  std::string ActionUsageSnippet(int action_idx) const;

  int AddMap(std::string name = "");
  bool RenameMap(const std::string& from, const std::string& to);
  void DeleteMap(const std::string& map);

  int AddAction(const std::string& map);
  int AddBinding(int action_idx);
  int AddComposite(int action_idx);
  void DeleteSelected();
  void DuplicateSelected();

  std::string SetActionName(int action_idx, std::string new_name, bool finalize = true);
  void SetActionType(int action_idx, InputAction::Type type);
  void SetActionVector2(int action_idx, bool vector2);

  void SetBindingPath(int action_idx, int binding_idx, const std::string& path,
                      bool coalesced = false);
  void SetCompositePartPath(int action_idx, int composite_idx, int part, int idx,
                            const std::string& path, bool coalesced = false);
  void AddCompositePartPath(int action_idx, int composite_idx, int part);
  void RemoveCompositePartPath(int action_idx, int composite_idx, int part, int idx);

  void SetGenerateHeader(bool enabled);
  void SetClassName(const std::string& class_name, bool coalesced = false);
  void SetHeaderPath(const std::string& header, bool coalesced = false);

  // Polls gamepad buttons / triggers while listening; returns control path on new press.
  static std::optional<std::string> PollGamepadListen(const InputManager* input,
                                                      bool prev_buttons[15]);

 private:
  void Checkpoint();
  void MarkChanged(bool coalesced = false);
  std::string UniqueActionName(const std::string& map, std::string base) const;
  std::string UniqueMapName(std::string base) const;

  Console& console_;
  std::shared_ptr<Project> project_;

  std::string path_;
  InputActionsDesc desc_;
  bool dirty_ = false;
  int map_ = 0;
  Selection selection_;
  std::vector<std::string> header_errors_;

  struct Snapshot {
    InputActionsDesc desc;
    int map = 0;
    Selection selection;
  };
  std::vector<Snapshot> undo_;
  std::vector<Snapshot> redo_;
  Snapshot last_committed_;
  bool edit_open_ = false;
  uint64_t state_revision_ = 1;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_INPUT_ACTIONS_WINDOW_H
