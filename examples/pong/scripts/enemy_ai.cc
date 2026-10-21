#include "enemy_ai.h"

#include <algorithm>

#include "paddle.h"

MERCURY_SCRIPT(EnemyAI);

void EnemyAI::OnStart() {
  paddle_ = GetBehaviour<Paddle>();
  if (!paddle_) PDebug::error("EnemyAI needs a Paddle on the same node");
  ball_ = Get(ball);
}

void EnemyAI::OnTick(double dt) {
  if (!paddle_ || !ball_) return;
  float diff = ball_.GetTransform()->GetPosition().y -
               GetTransform().GetPosition().y;
  paddle_->Move(std::clamp(diff * gain, -speed, speed));
}
