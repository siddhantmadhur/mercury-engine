//
// Created by smadhur on 5/10/2026.
//

#ifndef PEQUODENGINE_UI_ELEMENT_H
#define PEQUODENGINE_UI_ELEMENT_H
#include <vector>
#include <glm/glm.hpp>
namespace Pequod {

enum class UIPivot {
  kTopCenter,
  kTopLeft,
  kTopRight,
  kMiddleCenter,
  kMiddleLeft,
  kMiddleRight,
  kBottomCenter,
  kBottomLeft,
  kBottomRight
};

class UIElement {
 public:
  UIElement();
  ~UIElement();

  UIElement* CreateDiv(glm::vec2 pos, glm::vec2 size, UIPivot pivot);

  void SetPadding(float new_val = 0.0);
  void SetMargin(float new_val = 0.0);

 private:
  std::vector<UIElement*> children_;

  float padding_ = 0.0;
  float margin_ = 0.0;
};

}  // namespace Pequod

#endif  // PEQUODENGINE_UI_ELEMENT_H
