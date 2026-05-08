//
// Created by Siddhant Madhur on 27/03/2026.
//

#include "pobject.h"

#include <format>

#include "debugger/debugger.h"

namespace Pequod {
PObject::PObject() {}

PObject::~PObject() {
  delete std::get<Transform*>(properties_);
  delete std::get<Mesh*>(properties_);
  delete std::get<Texture2D*>(properties_);
}

void PObject::AddChild(uint64_t child_id) { children.push_back(child_id); }

}  // namespace Pequod
