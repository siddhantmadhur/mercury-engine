#ifndef MODEL_VIEWER_SCENE_HUD_H
#define MODEL_VIEWER_SCENE_HUD_H

#include <string>

#include "scripting/behaviour.h"

// The overlay of every scene map (ui/hud.uxml): the scene's name, how to
// move the camera, the frame rate and, with a FlythroughCamera, its lap
// results. Esc or the Back button returns to the menu.
class SceneHud : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("title", title);
    r.Field("hint", hint).Tooltip("Controls shown under the title");
    r.Field("camera", camera).Tooltip("Camera node (its FlythroughCamera reports laps)");
  }
  // Values hud.uxml binds to.
  void ReflectData(Mercury::Reflector& r) override;

  void OnStart() override;
  void OnFrame(double dt) override;
  void OnTick(double dt) override;

  std::string title = "Scene";
  std::string hint;
  Mercury::NodeRef camera{"Camera"};

 private:
  void BackToMenu();

  // Frame rate over the last half second.
  int frames_ = 0;
  float elapsed_ = 0.0f;
  std::string fps_text_ = "-- fps";
};

#endif  // MODEL_VIEWER_SCENE_HUD_H
