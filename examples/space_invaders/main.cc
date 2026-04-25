//
// Created by smadhur on 4/25/2026.
//

#include "application/d3d11_application.h"
#include "space_invaders.h"

#include <memory>
using namespace Pequod;

int main(void) {
  auto game_win = std::make_unique<D3D11Application>(
      std::format("Space Invaders [Pequod {}]", PEQUOD_ENGINE_VERSION), 720,
      1080);

  auto space_invaders = std::make_unique<SpaceInvaders>();

  game_win->SetGameScene(std::move(space_invaders));

  return game_win->Run();
}
