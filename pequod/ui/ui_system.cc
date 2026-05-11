//
// Created by smadhur on 5/10/2026.
//

#include "ui_system.h"

namespace Pequod {
UISystem::UISystem() { this->root_ = new UIElement(); }

UIElement* UISystem::GetRoot() { return this->root_; }

UISystem::~UISystem() { delete root_; }
}  // namespace Pequod