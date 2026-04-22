//
// Created by smadhur on 3/31/26.
//

#include "collision_body.h"

namespace Pequod {
CollisionBody::CollisionBody() {}
JPH::ShapeRefC CollisionBody::GetShapeRef() const { return this->shape_ref_c_; }
}  // namespace Pequod
