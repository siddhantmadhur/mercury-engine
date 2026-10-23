#include "banana_pickup.h"

#include <glm/glm.hpp>

#include "game_manager.h"
#include "player_ball.h"

MERCURY_SCRIPT(BananaPickup);

void BananaPickup::OnTick(double dt) {
  auto& transform = GetTransform();
  transform.SetRotate(transform.GetRotate() +
                      glm::vec3(0.0f, rotate_speed * static_cast<float>(dt), 0.0f));
}

void BananaPickup::OnTriggerEnter(Mercury::GameObject other) {
  if (collected_) return;
  if (!other.GetBehaviour<PlayerBall>()) return;

  collected_ = true;
  if (auto mgr_obj = Get(game_manager)) {
    if (auto mgr = mgr_obj.GetBehaviour<GameManager>()) {
      mgr->CollectBanana(points);
    }
  } else {
    // If NodeRef wasn't set, find by scene search
    auto& registry = GetScene().GetRegistry();
    for (auto entity : registry.view<Mercury::NodeInfo>()) {
      if (auto mgr = GetScene().GetGameObject(entity).GetBehaviour<GameManager>()) {
        mgr->CollectBanana(points);
        break;
      }
    }
  }
  Destroy(GetGameObject());
}
