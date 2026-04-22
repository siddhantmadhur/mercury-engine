//
// Created by smadhur on 4/22/2026.
//

#ifndef PEQUODENGINE_INPUT_MANAGER_H
#define PEQUODENGINE_INPUT_MANAGER_H
#include <map>

namespace Pequod {

using Key = int;

/**
 * @brief Get inputs from application
 */
class InputManager {
 public:
  InputManager();
  bool IsPressed(Key k);
  bool IsJustPressed(Key k);
  void SetKeyDown(Key k);
  void SetKeyUp(Key k);

 private:
  std::map<int, bool> key_status_ = {};
};

}  // namespace Pequod

#endif  // PEQUODENGINE_INPUT_MANAGER_H
