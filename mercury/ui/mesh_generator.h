/*
 * mesh_generator.h
 *
 * Turns elements into triangles. Every shape is tessellated on the CPU with
 * a one-pixel anti-aliased fringe, in render-target pixels.
 *
 * Custom drawing goes through MeshGenerationContext, like Unity's
 * generateVisualContent:
 *
 *   element->generateVisualContent = [](MeshGenerationContext& ctx) {
 *     auto& p = ctx.painter2D();
 *     p.strokeColor = Color(1, 1, 1);
 *     p.lineWidth = 4;
 *     p.BeginPath();
 *     p.Arc({50, 50}, 40, 0, 270);
 *     p.Stroke();
 *   };
 */
#ifndef MERCURY_UI_MESH_GENERATOR_H
#define MERCURY_UI_MESH_GENERATOR_H

#include <array>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

#include "ui/draw_data.h"
#include "ui/events.h"
#include "ui/font.h"
#include "ui/style.h"

namespace Mercury::UI {

class VisualElement;

/**
 * @brief Collects triangles into a UIDrawData, batching by texture and clip
 */
class MeshBuilder {
 public:
  explicit MeshBuilder(UIDrawData& out) : out_(out) {}

  // Clip rectangle in pixels: x, y, w, h (width < 0 means none).
  void SetClip(glm::ivec4 clip) { clip_ = clip; }
  glm::ivec4 clip() const { return clip_; }

  // Appends triangles; texture 0 means the shape is untextured and can
  // share a batch with anything.
  void AddTriangles(const UIVertex* vertices, size_t vertex_count,
                    const uint32_t* indices, size_t index_count,
                    uint32_t texture_id);

  // Shapes in pixel space. Points go around the shape in either direction.
  void FillConvex(const std::vector<glm::vec2>& points, uint32_t color,
                  bool anti_alias = true);
  // The band between two contours with the same number of points.
  void FillRing(const std::vector<glm::vec2>& outer,
                const std::vector<glm::vec2>& inner,
                const std::vector<uint32_t>& colors, bool anti_alias = true);
  // A textured quad: corners in order top-left, top-right, bottom-right,
  // bottom-left.
  void AddQuad(const std::array<glm::vec2, 4>& corners, glm::vec4 uv,
               uint32_t color, uint32_t texture_id,
               VertexMode mode = VertexMode::kTextured,
               glm::vec4 params = glm::vec4(0.0f), uint32_t outline_color = 0);
  // A thick polyline.
  void Stroke(const std::vector<glm::vec2>& points, float width, uint32_t color,
              bool closed);
  // Fills any simple polygon (ear clipping).
  void FillPolygon(const std::vector<glm::vec2>& points, uint32_t color);

 private:
  UIDrawCommand& CommandFor(uint32_t texture_id);

  UIDrawData& out_;
  glm::ivec4 clip_{0, 0, -1, -1};
};

class Painter2D {
 public:
  Color fillColor = Color::White();
  Color strokeColor = Color::Black();
  float lineWidth = 1.0f;

  void BeginPath();
  void MoveTo(glm::vec2 p);
  void LineTo(glm::vec2 p);
  // Angles in degrees, clockwise from +x (y points down).
  void Arc(glm::vec2 center, float radius, float start_degrees,
           float end_degrees, bool counter_clockwise = false);
  void QuadraticCurveTo(glm::vec2 control, glm::vec2 p);
  void BezierCurveTo(glm::vec2 c1, glm::vec2 c2, glm::vec2 p);
  void ClosePath();
  void Fill();
  void Stroke();

 private:
  friend class MeshGenerationContext;
  struct SubPath {
    std::vector<glm::vec2> points;
    bool closed = false;
  };
  SubPath& Current();
  MeshBuilder* builder_ = nullptr;
  glm::mat3 transform_{1.0f};
  float opacity_ = 1.0f;
  std::vector<SubPath> paths_;
};

/**
 * @brief Drawing in an element's local coordinates
 */
class MeshGenerationContext {
 public:
  MeshGenerationContext(MeshBuilder& builder, VisualElement* element,
                        const glm::mat3& to_pixels, float opacity);

  VisualElement* visualElement() const { return element_; }
  Painter2D& painter2D() { return painter_; }

  void DrawRect(const Rect& rect, Color color);
  void DrawRoundedRect(const Rect& rect, const std::array<float, 4>& radii,
                       Color color);
  void DrawImage(const Rect& rect, uint32_t texture_id, glm::vec4 uv = {0, 0, 1, 1},
                 Color tint = Color::White());
  // Draws laid-out text with its top-left at `origin`.
  void DrawText(const TextLayout& layout, const Font& font, glm::vec2 origin,
                const ComputedStyle& style);

  // Local -> pixel transform and the inherited opacity.
  const glm::mat3& transform() const { return to_pixels_; }
  float opacity() const { return opacity_; }
  glm::vec2 ToPixels(glm::vec2 local) const;
  MeshBuilder& builder() { return builder_; }

 private:
  MeshBuilder& builder_;
  VisualElement* element_;
  glm::mat3 to_pixels_;
  float opacity_;
  Painter2D painter_;
};

/**
 * @brief Points around a rounded rectangle, with an extra copy at the middle
 * of each corner so per-side colors can change sharply there
 * @param radii (rx, ry) per corner: top-left, top-right, bottom-right,
 * bottom-left
 * @param segments per corner (even)
 * @param sides receives the side (0 left, 1 top, 2 right, 3 bottom) of each
 * point
 */
void BuildRoundedContour(const Rect& rect, const std::array<glm::vec2, 4>& radii,
                         const std::array<int, 4>& segments,
                         std::vector<glm::vec2>& points, std::vector<int>* sides);

// Draws an element's background, image and border.
void DrawElementBox(MeshGenerationContext& context, const VisualElement& element);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_MESH_GENERATOR_H
