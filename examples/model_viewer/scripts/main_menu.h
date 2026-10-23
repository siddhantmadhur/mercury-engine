#ifndef MODEL_VIEWER_MAIN_MENU_H
#define MODEL_VIEWER_MAIN_MENU_H

#include <string>

#include "scripting/behaviour.h"

// The first map: a button per scene (ui/menu.uxml). Each button is named
// after its map, maps/<name>.mmap. Esc quits.
class MainMenu : public Mercury::Behaviour {
 public:
  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

 private:
  void Open(const std::string& map, const std::string& title);

  std::string pending_;  // map to load once "Loading..." has been drawn
  int wait_frames_ = 0;
};

#endif  // MODEL_VIEWER_MAIN_MENU_H
