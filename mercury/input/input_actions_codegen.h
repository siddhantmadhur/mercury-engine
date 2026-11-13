/*
 * input_actions_codegen.h
 *
 * Unity's "Generate C# Class" for .minput files: an AngelScript namespace with
 * one constant per action, so scripts name actions with a compile-time symbol
 * and a renamed or deleted action fails to compile instead of returning null.
 *
 *   InputAction jump = Input::RequireAction(GameInput::Player::Jump);
 *   if (jump.wasPressedThisFrame) Jump();
 */
#ifndef MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
#define MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H

#include <string>
#include <string_view>
#include <vector>

#include "input/input_actions.h"

namespace Mercury {

// An AngelScript identifier for a map or action name: "move left" ->
// "MoveLeft".
std::string InputActionIdentifier(std::string_view name);

struct InputActionsSource {
  std::string source;
  // Names that map to the same identifier; the file is not usable if set.
  std::vector<std::string> errors;
};

InputActionsSource GenerateInputActionsAngelScript(
    const InputActionsDesc& desc, std::string_view class_name,
    std::string_view source_path);

}  // namespace Mercury

#endif  // MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
