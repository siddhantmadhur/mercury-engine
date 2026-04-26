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
bool InputManager::IsJustPressed(Key k) {
  if (fresh_presses_.contains(k)) {
    return fresh_presses_[k];
  } else {
    return false;
  }
}
void InputManager::SetKeyDown(Key k) {
  key_status_[k] = true;
  fresh_presses_[k] = true;
}
void InputManager::SetKeyUp(Key k) {
  key_status_[k] = false;
  repeated_keys_[k] = false;
  fresh_presses_[k] = false;
}
void InputManager::ResetFreshPresses() { fresh_presses_.clear(); }
void InputManager::SetKeyRepeat(Key k) { repeated_keys_[k] = true; }

}  // namespace Pequod
