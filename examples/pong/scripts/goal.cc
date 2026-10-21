#include "goal.h"

#include "ball.h"
#include "pong_game.h"

MERCURY_SCRIPT(Goal);

void Goal::OnCollisionEnter(Mercury::GameObject other) {
  if (!other.GetBehaviour<Ball>()) return;
  if (auto pong = Get(game).GetBehaviour<PongGame>()) pong->Score(scorer);
}
