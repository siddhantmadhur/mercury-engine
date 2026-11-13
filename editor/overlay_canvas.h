/*
 * overlay_canvas.h
 *
 * Lightweight 2D vector drawing interface used by SceneView gizmos/overlays
 * so 3D projection & handle logic remains independent of any UI toolkit.
 */
#ifndef MERCURY_EDITOR_OVERLAY_CANVAS_H
#define MERCURY_EDITOR_OVERLAY_CANVAS_H

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

namespace Mercury {

struct OverlayColor {
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  uint8_t a = 255;

  static OverlayColor FromFloat(float rf, float gf, float bf, float af = 1.0f) {
    auto clamp = [](float v) -> uint8_t {
      if (v <= 0.0f) return 0;
      if (v >= 1.0f) return 255;
      return static_cast<uint8_t>(v * 255.0f + 0.5f);
    };
    return {clamp(rf), clamp(gf), clamp(bf), clamp(af)};
  }
};

class OverlayCanvas {
 public:
  virtual ~OverlayCanvas() = default;

  virtual void AddLine(glm::vec2 a, glm::vec2 b, OverlayColor color,
                       float thickness = 1.0f) = 0;
  virtual void AddCircle(glm::vec2 center, float radius, OverlayColor color,
                         float thickness = 1.0f) = 0;
  virtual void AddCircleFilled(glm::vec2 center, float radius,
                               OverlayColor color) = 0;
  virtual void AddRect(glm::vec2 min, glm::vec2 max, OverlayColor color,
                       float rounding = 0.0f, float thickness = 1.0f) = 0;
  virtual void AddRectFilled(glm::vec2 min, glm::vec2 max, OverlayColor color,
                             float rounding = 0.0f) = 0;
  virtual void AddQuad(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3,
                       OverlayColor color, float thickness = 1.0f) = 0;
  virtual void AddQuadFilled(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                             glm::vec2 p3, OverlayColor color) = 0;
  virtual void AddTriangleFilled(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2,
                                 OverlayColor color) = 0;
  virtual void AddText(glm::vec2 pos, OverlayColor color,
                       const std::string& text) = 0;
  virtual glm::vec2 MeasureText(const std::string& text) const = 0;
};

}  // namespace Mercury

#endif  // MERCURY_EDITOR_OVERLAY_CANVAS_H
