//
// Created by smadhur on 5/10/2026.
//

#ifndef PEQUODENGINE_UI_SYSTEM_H
#define PEQUODENGINE_UI_SYSTEM_H
#include "ui_element.h"
#include <glm/glm.hpp>

#include "globals.h"
#include "pobject/texture_atlas.h"
namespace Pequod {

/**
 * UI System is a retained-mode ui library that is renderer-agnostic. It is
 * independent of PObject implementation and uses its own shader files so it
 * doesn't have any dependencies.
 *
 * Again, since it's retained, it should not be used like imgui where it is
 * called every frame. Rather, creating an element once will keep it existing on
 * the system until it is closed
 *
 * Another important not is that it does not by itself handle creating new
 * objects, or anything on the tree. Instead, it returns a pointer to the top
 * root of the system, and using it you can draw the layout you want.
 */
class UISystem {
 public:
  UISystem();
  ~UISystem();

  UIElement* GetRoot();

  std::vector<StaticVertex> GetVertexBuffer();
  TextureAtlas texture_atlas_;

 private:
  // The root element cannot be modified at takes up the whole screen
  UIElement* root_ = nullptr;
};
}  // namespace Pequod

#endif  // PEQUODENGINE_UI_SYSTEM_H
