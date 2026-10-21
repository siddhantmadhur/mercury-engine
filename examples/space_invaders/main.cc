//
// Created by smadhur on 4/25/2026.
//

#include "application/default_application.h"
#include "space_invaders.h"

#include <format>
#include <memory>
using namespace Mercury;

int main(void) {
  auto game_win = std::make_unique<DefaultApplication>(
      std::format("Space Invaders [Mercury {}]", MERCURY_ENGINE_VERSION),
      224 * ZOOM, 256 * ZOOM);

  auto space_invaders = std::make_unique<SpaceInvaders>();

  game_win->SetGameScene(std::move(space_invaders));

  return game_win->Run();
}
