/*
 * input_actions_codegen.h
 *
 * Unity's "Generate C# Class" for .minput files: a C++ header with one
 * struct per action map, so scripts name actions as members and a renamed
 * or deleted action fails to compile instead of returning null.
 *
 *   GameInput input(*GetInputActions());
 *   if (input.Player.Jump->WasPressedThisFrame()) Jump();
 *   glm::vec2 move = input.Player.Move->ReadValue<glm::vec2>();
 */
#ifndef MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
#define MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H

#include <string>
#include <string_view>
#include <vector>

#include "input/input_actions.h"

namespace Mercury {

// A C++ identifier for a map or action name: "move left" -> "MoveLeft".
std::string InputActionIdentifier(std::string_view name);

struct InputActionsHeader {
  std::string source;
  // Names that map to the same identifier; the header is not usable if set.
  std::vector<std::string> errors;
};

InputActionsHeader GenerateInputActionsHeader(const InputActionsDesc& desc,
                                              std::string_view class_name,
                                              std::string_view source_path);

}  // namespace Mercury

#endif  // MERCURY_INPUT_INPUT_ACTIONS_CODEGEN_H
