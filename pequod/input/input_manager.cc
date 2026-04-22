//
// Created by smadhur on 4/22/2026.
//

#include "input_manager.h"

namespace Pequod {
InputManager::InputManager() {}
bool InputManager::IsPressed(Key k) {
  if (key_status_.contains(k)) {
    return key_status_[k];
  } else {
    return false;
  }
}
bool InputManager::IsJustPressed(Key k) { return IsPressed(k); }
void InputManager::SetKeyDown(Key k) { key_status_[k] = true; }
void InputManager::SetKeyUp(Key k) { key_status_[k] = false; }

}  // namespace Pequod
