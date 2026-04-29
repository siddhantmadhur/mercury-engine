//
// Created by smadhur on 4/25/2026.
//

#include "application/default_application.h"
#include "breakout_scene.h"
int main() {
  auto application = DefaultApplication(
      std::format("Breakout [Pequod {}]", PEQUOD_ENGINE_VERSION));
  auto breakout_scene = std::make_unique<BreakoutScene>();
  application.SetGameScene(std::move(breakout_scene));
  return application.Run();
}