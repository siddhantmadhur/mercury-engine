/*
 * layout.h
 *
 * Flexbox layout through Yoga, the engine Unity UI Toolkit uses.
 */
#ifndef MERCURY_UI_LAYOUT_H
#define MERCURY_UI_LAYOUT_H

#include <vector>

#include "ui/events.h"

struct YGConfig;

namespace Mercury::UI {

class VisualElement;

YGConfig* SharedYogaConfig();

struct GeometryChange {
  VisualElement* element;
  Rect old_rect;
  Rect new_rect;
};

// True if Yoga needs to run again for `root`.
bool IsLayoutDirty(VisualElement* root);

/**
 * @brief Lays out `root` at the given size and stores each element's rect
 * @param changes elements whose rect changed
 */
void CalculateLayout(VisualElement* root, float width, float height,
                     std::vector<GeometryChange>& changes);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_LAYOUT_H
