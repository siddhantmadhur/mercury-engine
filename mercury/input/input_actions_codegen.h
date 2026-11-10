/*
 * input_actions_codegen.h
 *
 * Unity's "Generate C# Class" for .minput files: a TypeScript class with one
 * member per action map, so scripts name actions as members and a renamed
 * or deleted action fails to compile instead of returning null.
 *
 *   const input = new GameInput(this.inputActions);
 *   if (input.Player.Jump.wasPressedThisFrame) this.jump();
 *   const move = input.Player.Move.readVector2();
 */
#ifndef MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
#define MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H

#include <string>
#include <string_view>
#include <vector>

#include "input/input_actions.h"

namespace Mercury {

// A TypeScript identifier for a map or action name: "move left" -> "MoveLeft".
std::string InputActionIdentifier(std::string_view name);

struct InputActionsSource {
  std::string source;
  // Names that map to the same identifier; the file is not usable if set.
  std::vector<std::string> errors;
};

InputActionsSource GenerateInputActionsTypeScript(const InputActionsDesc& desc,
                                                  std::string_view class_name,
                                                  std::string_view source_path);

}  // namespace Mercury

#endif  // MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
