#include "pickup.h"

#include "game_manager.h"
#include "player_controller.h"

MERCURY_SCRIPT(Pickup);

void Pickup::OnTriggerEnter(Mercury::GameObject other) {
  if (collected_ || !other.GetBehaviour<PlayerController>()) return;
  collected_ = true;
  if (auto manager = Get(game).GetBehaviour<GameManager>()) {
    manager->Collect(points);
  }
  Destroy(GetGameObject());
}
