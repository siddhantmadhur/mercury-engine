#include <Jolt/Jolt.h>

#include <iostream>
#include <thread>

#include "application/application.h"
#include "application/default_application.h"
#include "pong_scene.h"

using namespace Pequod;

int main(void) {
  auto pong_win = std::make_unique<DefaultApplication>("Pong");

  auto pong_scene = std::make_unique<PongScene>();

  pong_win->SetGameScene(std::move(pong_scene));

  return pong_win->Run();
}