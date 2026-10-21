#include "camera_follow.h"

MERCURY_SCRIPT(CameraFollow);

void CameraFollow::OnStart() {
  target_ = Get(target);
  if (target_) {
    offset_ = GetTransform().GetPosition() -
              target_.GetTransform()->GetPosition();
  }
}

void CameraFollow::OnTick(double dt) {
  if (!target_) return;
  GetTransform().SetPosition(target_.GetTransform()->GetPosition() + offset_);
}
