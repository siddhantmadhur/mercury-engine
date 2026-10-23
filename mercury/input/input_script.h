/*
 * input_script.h
 *
 * Replays scripted input into a running game, so it can be played without a
 * person at the keyboard (smoke tests, screenshots of gameplay). The player
 * reads the file named by MERCURY_INPUT_SCRIPT. Lines:
 *
 *   <frame> press <key>        (hold a key: w, space, leftShift, f5, 1, ...)
 *   <frame> release <key>
 *   <frame> tap <key>          (press now, release next frame)
 *   <frame> mouse <left|right|middle> <down|up>
 *   <frame> look <dx> <dy>     (mouse movement in window pixels)
 *   <frame> capture <file.png> (save this frame)
 *   <frame> log <text...>
 *   <frame> quit
 *   # comments and blank lines are ignored
 *
 * Key names are the Unity control names the input actions use. While a
 * script runs, every frame advances time by exactly 1/60 s so replays are
 * repeatable, and the real mouse does not move the view.
 */
#ifndef MERCURY_INPUT_INPUT_SCRIPT_H
#define MERCURY_INPUT_INPUT_SCRIPT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Mercury {

class InputManager;

class InputScript {
 public:
  struct Step {
    uint64_t frame = 0;
    std::string command;
    std::vector<std::string> args;
    std::string rest;  // everything after the command
    int line = 0;
  };

  // False (with `error` set) if the file cannot be read or has a bad line.
  bool Load(const std::string& file, std::string* error);
  bool LoadFromString(const std::string& text, std::string* error);

  struct Hooks {
    std::function<void(const std::string& file)> capture;
    std::function<void()> quit;
  };

  // Runs every step due on `frame`. Call once per frame after polling
  // window events.
  void Apply(uint64_t frame, InputManager& input, GLFWwindow* window,
             const Hooks& hooks);

  bool IsEmpty() const { return steps_.empty(); }

 private:
  std::vector<Step> steps_;
  size_t next_ = 0;
  std::vector<int> release_next_;  // keys tapped last frame
};

}  // namespace Mercury

#endif  // MERCURY_INPUT_INPUT_SCRIPT_H
