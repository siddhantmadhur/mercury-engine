//
// Created by smadhur on 5/10/2026.
//

#include "ui_element.h"
namespace Pequod {

UIElement::UIElement() {}
UIElement::~UIElement() {
  for (int i = 0; i < children_.size(); i++) {
    delete children_[i];
  }
}

}  // namespace Pequod