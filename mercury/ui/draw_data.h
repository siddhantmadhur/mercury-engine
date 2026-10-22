/*
 * draw_data.h
 *
 * What a renderer needs to draw a UI: triangles in render-target pixels
 * (origin at the top left, y down) split into commands by texture and clip
 * rectangle. Backends draw it after the scene, with alpha blending and no
 * depth test.
 */
#ifndef MERCURY_UI_DRAW_DATA_H
#define MERCURY_UI_DRAW_DATA_H

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Mercury::UI {

enum class VertexMode : uint32_t {
  kSolid = 0,     // vertex color only
  kTextured = 1,  // texture * color
  kText = 2,      // signed-distance-field glyph
};

struct UIVertex {
  glm::vec2 position;  // pixels
  glm::vec2 uv;
  uint32_t color;          // RGBA8, sRGB, not premultiplied
  uint32_t outline_color;  // RGBA8, for text outlines
  // x: VertexMode, y: SDF edge (0.5 is normal weight, lower is bolder),
  // z: outline width in SDF units, w: SDF softness (0 = automatic)
  glm::vec4 params;
};
static_assert(sizeof(UIVertex) == 40);

struct UIDrawCommand {
  uint32_t texture_id = 0;  // UITexture id; 0 is a 1x1 white texture
  // Clip rectangle in pixels: x, y, width, height.
  glm::ivec4 clip{0, 0, 0, 0};
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
};

struct UIDrawData {
  std::vector<UIVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<UIDrawCommand> commands;
  glm::ivec2 target_size{0, 0};

  bool empty() const { return commands.empty(); }
  void Clear() {
    vertices.clear();
    indices.clear();
    commands.clear();
  }
  // Appends another draw list (used to merge panels).
  void Append(const UIDrawData& other);
};

inline uint32_t PackColor(glm::vec4 c) {
  auto byte = [](float f) {
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    return static_cast<uint32_t>(f * 255.0f + 0.5f);
  };
  return byte(c.r) | (byte(c.g) << 8) | (byte(c.b) << 16) | (byte(c.a) << 24);
}

}  // namespace Mercury::UI

#endif  // MERCURY_UI_DRAW_DATA_H
