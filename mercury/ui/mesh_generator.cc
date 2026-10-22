#include "mesh_generator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "ui/panel.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

namespace {

constexpr float kAA = 1.0f;  // fringe width in pixels

glm::vec2 Apply(const glm::mat3& m, glm::vec2 p) {
  return glm::vec2(m * glm::vec3(p, 1.0f));
}

uint32_t WithAlpha(uint32_t color, float alpha_scale) {
  const uint32_t a = color >> 24;
  const uint32_t scaled = static_cast<uint32_t>(std::clamp(a * alpha_scale, 0.0f, 255.0f) + 0.5f);
  return (color & 0x00FFFFFF) | (scaled << 24);
}

uint32_t Pack(const Color& c, float opacity) {
  return PackColor(glm::vec4(c.rgba.r, c.rgba.g, c.rgba.b, c.rgba.a * opacity));
}

float SignedArea(const std::vector<glm::vec2>& p) {
  float area = 0.0f;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    area += (p[j].x * p[i].y - p[i].x * p[j].y);
  }
  return area / 2.0f;
}

// Outward vertex normals, scaled for a constant-width fringe (miter).
std::vector<glm::vec2> VertexNormals(const std::vector<glm::vec2>& p, bool closed) {
  const size_t n = p.size();
  // In y-down space, (d.y, -d.x) points out of a clockwise contour.
  const float orientation = SignedArea(p) >= 0.0f ? 1.0f : -1.0f;
  constexpr float kSame = 1e-4f;
  auto edge_normal = [&](glm::vec2 from, glm::vec2 to) {
    const glm::vec2 d = to - from;
    const float len = glm::length(d);
    return len > kSame ? glm::vec2(d.y, -d.x) / len * orientation : glm::vec2(0.0f);
  };
  std::vector<glm::vec2> normals(n);
  for (size_t i = 0; i < n; i++) {
    // Neighbours at a different position, so repeated points (used for sharp
    // color changes) share one normal and leave no gaps.
    size_t prev = i, next = i;
    for (size_t k = 1; k < n; k++) {
      const size_t j = (i + n - k) % n;
      if (!closed && j > i) break;
      if (glm::distance(p[j], p[i]) > kSame) {
        prev = j;
        break;
      }
    }
    for (size_t k = 1; k < n; k++) {
      const size_t j = (i + k) % n;
      if (!closed && j < i) break;
      if (glm::distance(p[j], p[i]) > kSame) {
        next = j;
        break;
      }
    }
    glm::vec2 a = prev != i ? edge_normal(p[prev], p[i]) : glm::vec2(0.0f);
    glm::vec2 b = next != i ? edge_normal(p[i], p[next]) : glm::vec2(0.0f);
    if (a == glm::vec2(0.0f)) a = b;
    if (b == glm::vec2(0.0f)) b = a;
    glm::vec2 m = a + b;
    const float len2 = glm::dot(m, m);
    if (len2 < 1e-6f) {
      normals[i] = b;
      continue;
    }
    m /= std::sqrt(len2);
    const float cos_half = std::max(glm::dot(m, b), 0.25f);  // limit spikes
    normals[i] = m / cos_half;
  }
  return normals;
}

int CornerSegments(float radius_px) {
  if (radius_px <= 0.01f) return 0;
  int n = static_cast<int>(std::ceil(radius_px / 2.5f));
  n = std::clamp(n, 2, 16);
  return n + (n % 2);
}

}  // namespace

// ---------------------------------------------------------- MeshBuilder ---

UIDrawCommand& MeshBuilder::CommandFor(uint32_t texture_id) {
  if (!out_.commands.empty()) {
    UIDrawCommand& last = out_.commands.back();
    if (last.clip == clip_) {
      if (texture_id == 0 || last.texture_id == texture_id) return last;
      if (last.texture_id == 0) {
        // An untextured batch can adopt the first texture it meets.
        last.texture_id = texture_id;
        return last;
      }
    }
  }
  UIDrawCommand command;
  command.texture_id = texture_id;
  command.clip = clip_;
  command.index_offset = static_cast<uint32_t>(out_.indices.size());
  out_.commands.push_back(command);
  return out_.commands.back();
}

void MeshBuilder::AddTriangles(const UIVertex* vertices, size_t vertex_count,
                               const uint32_t* indices, size_t index_count,
                               uint32_t texture_id) {
  if (vertex_count == 0 || index_count == 0) return;
  UIDrawCommand& command = CommandFor(texture_id);
  const uint32_t base = static_cast<uint32_t>(out_.vertices.size());
  out_.vertices.insert(out_.vertices.end(), vertices, vertices + vertex_count);
  for (size_t i = 0; i < index_count; i++) out_.indices.push_back(base + indices[i]);
  command.index_count += static_cast<uint32_t>(index_count);
}

void MeshBuilder::FillConvex(const std::vector<glm::vec2>& points, uint32_t color,
                             bool anti_alias) {
  const size_t n = points.size();
  if (n < 3 || (color >> 24) == 0) return;
  std::vector<UIVertex> v;
  std::vector<uint32_t> idx;
  const glm::vec4 solid(static_cast<float>(VertexMode::kSolid), 0, 0, 0);
  if (!anti_alias) {
    for (const auto& p : points) v.push_back({p, {0, 0}, color, 0, solid});
    for (uint32_t i = 1; i + 1 < n; i++) idx.insert(idx.end(), {0, i, i + 1});
  } else {
    const auto normals = VertexNormals(points, true);
    const uint32_t transparent = color & 0x00FFFFFF;
    for (size_t i = 0; i < n; i++) {
      v.push_back({points[i] - normals[i] * (kAA * 0.5f), {0, 0}, color, 0, solid});
      v.push_back({points[i] + normals[i] * (kAA * 0.5f), {0, 0}, transparent, 0, solid});
    }
    for (uint32_t i = 1; i + 1 < n; i++) idx.insert(idx.end(), {0, i * 2, (i + 1) * 2});
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t j = static_cast<uint32_t>((i + 1) % n);
      idx.insert(idx.end(), {i * 2, j * 2, j * 2 + 1, i * 2, j * 2 + 1, i * 2 + 1});
    }
  }
  AddTriangles(v.data(), v.size(), idx.data(), idx.size(), 0);
}

void MeshBuilder::FillRing(const std::vector<glm::vec2>& outer,
                           const std::vector<glm::vec2>& inner,
                           const std::vector<uint32_t>& colors, bool anti_alias) {
  const size_t n = outer.size();
  if (n < 3 || inner.size() != n || colors.size() != n) return;
  std::vector<UIVertex> v;
  std::vector<uint32_t> idx;
  const glm::vec4 solid(static_cast<float>(VertexMode::kSolid), 0, 0, 0);
  const auto out_normals = VertexNormals(outer, true);
  const auto in_normals = VertexNormals(inner, true);
  const float h = anti_alias ? kAA * 0.5f : 0.0f;
  // Per point: outer fringe, outer, inner, inner fringe.
  for (size_t i = 0; i < n; i++) {
    const uint32_t c = colors[i];
    const uint32_t t = c & 0x00FFFFFF;
    v.push_back({outer[i] + out_normals[i] * h, {0, 0}, t, 0, solid});
    v.push_back({outer[i] - out_normals[i] * h, {0, 0}, c, 0, solid});
    v.push_back({inner[i] + in_normals[i] * h, {0, 0}, c, 0, solid});
    v.push_back({inner[i] - in_normals[i] * h, {0, 0}, t, 0, solid});
  }
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t j = static_cast<uint32_t>((i + 1) % n);
    for (uint32_t k = 0; k < 3; k++) {
      if (!anti_alias && k != 1) continue;
      const uint32_t a = i * 4 + k, b = i * 4 + k + 1;
      const uint32_t c = j * 4 + k, d = j * 4 + k + 1;
      idx.insert(idx.end(), {a, c, d, a, d, b});
    }
  }
  AddTriangles(v.data(), v.size(), idx.data(), idx.size(), 0);
}

void MeshBuilder::AddQuad(const std::array<glm::vec2, 4>& c, glm::vec4 uv,
                          uint32_t color, uint32_t texture_id, VertexMode mode,
                          glm::vec4 params, uint32_t outline_color) {
  params.x = static_cast<float>(mode);
  const UIVertex v[4] = {
      {c[0], {uv.x, uv.y}, color, outline_color, params},
      {c[1], {uv.z, uv.y}, color, outline_color, params},
      {c[2], {uv.z, uv.w}, color, outline_color, params},
      {c[3], {uv.x, uv.w}, color, outline_color, params},
  };
  const uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
  AddTriangles(v, 4, idx, 6, texture_id);
}

void MeshBuilder::Stroke(const std::vector<glm::vec2>& points, float width,
                         uint32_t color, bool closed) {
  const size_t n = points.size();
  if (n < 2 || width <= 0.0f || (color >> 24) == 0) return;
  const auto normals = VertexNormals(points, closed);
  const glm::vec4 solid(static_cast<float>(VertexMode::kSolid), 0, 0, 0);
  const float half = width / 2.0f;
  const uint32_t transparent = color & 0x00FFFFFF;
  // Thin lines fade instead of shrinking below a pixel.
  const uint32_t core = width < 1.0f ? WithAlpha(color, width) : color;
  const float core_half = std::max(half - kAA * 0.5f, 0.0f);
  std::vector<UIVertex> v;
  std::vector<uint32_t> idx;
  for (size_t i = 0; i < n; i++) {
    glm::vec2 nrm = normals[i];
    if (!closed && (i == 0 || i == n - 1)) {
      const glm::vec2 d = i == 0 ? points[1] - points[0] : points[n - 1] - points[n - 2];
      const float len = glm::length(d);
      nrm = len > 0 ? glm::vec2(d.y, -d.x) / len : glm::vec2(0, 1);
    }
    v.push_back({points[i] + nrm * (core_half + kAA), {0, 0}, transparent, 0, solid});
    v.push_back({points[i] + nrm * core_half, {0, 0}, core, 0, solid});
    v.push_back({points[i] - nrm * core_half, {0, 0}, core, 0, solid});
    v.push_back({points[i] - nrm * (core_half + kAA), {0, 0}, transparent, 0, solid});
  }
  const size_t segments = closed ? n : n - 1;
  for (uint32_t i = 0; i < segments; i++) {
    const uint32_t j = static_cast<uint32_t>((i + 1) % n);
    for (uint32_t k = 0; k < 3; k++) {
      const uint32_t a = i * 4 + k, b = i * 4 + k + 1;
      const uint32_t c = j * 4 + k, d = j * 4 + k + 1;
      idx.insert(idx.end(), {a, c, d, a, d, b});
    }
  }
  AddTriangles(v.data(), v.size(), idx.data(), idx.size(), 0);
}

void MeshBuilder::FillPolygon(const std::vector<glm::vec2>& points, uint32_t color) {
  const size_t n = points.size();
  if (n < 3 || (color >> 24) == 0) return;
  const glm::vec4 solid(static_cast<float>(VertexMode::kSolid), 0, 0, 0);
  std::vector<UIVertex> v;
  for (const auto& p : points) v.push_back({p, {0, 0}, color, 0, solid});
  // Ear clipping.
  std::vector<uint32_t> remaining(n), idx;
  for (uint32_t i = 0; i < n; i++) remaining[i] = i;
  const float orientation = SignedArea(points) >= 0 ? 1.0f : -1.0f;
  auto cross = [](glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; };
  size_t guard = 0;
  while (remaining.size() > 3 && guard++ < n * n) {
    bool clipped = false;
    for (size_t i = 0; i < remaining.size(); i++) {
      const uint32_t a = remaining[(i + remaining.size() - 1) % remaining.size()];
      const uint32_t b = remaining[i];
      const uint32_t c = remaining[(i + 1) % remaining.size()];
      const glm::vec2 pa = points[a], pb = points[b], pc = points[c];
      if (cross(pb - pa, pc - pb) * orientation <= 0) continue;  // reflex
      bool inside = false;
      for (uint32_t k : remaining) {
        if (k == a || k == b || k == c) continue;
        const glm::vec2 p = points[k];
        const float d1 = cross(pb - pa, p - pa) * orientation;
        const float d2 = cross(pc - pb, p - pb) * orientation;
        const float d3 = cross(pa - pc, p - pc) * orientation;
        if (d1 > 0 && d2 > 0 && d3 > 0) {
          inside = true;
          break;
        }
      }
      if (inside) continue;
      idx.insert(idx.end(), {a, b, c});
      remaining.erase(remaining.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) break;  // degenerate polygon
  }
  if (remaining.size() == 3) idx.insert(idx.end(), {remaining[0], remaining[1], remaining[2]});
  AddTriangles(v.data(), v.size(), idx.data(), idx.size(), 0);
}

// ------------------------------------------------------------ Painter2D ---

Painter2D::SubPath& Painter2D::Current() {
  if (paths_.empty() || paths_.back().closed) paths_.emplace_back();
  return paths_.back();
}

void Painter2D::BeginPath() { paths_.clear(); }

void Painter2D::MoveTo(glm::vec2 p) {
  paths_.emplace_back();
  paths_.back().points.push_back(p);
}

void Painter2D::LineTo(glm::vec2 p) { Current().points.push_back(p); }

void Painter2D::Arc(glm::vec2 center, float radius, float start, float end,
                    bool counter_clockwise) {
  float sweep = end - start;
  if (counter_clockwise && sweep > 0) sweep -= 360.0f;
  if (!counter_clockwise && sweep < 0) sweep += 360.0f;
  const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / 6.0f)));
  auto& path = Current();
  for (int i = 0; i <= steps; i++) {
    const float a = glm::radians(start + sweep * i / steps);
    path.points.push_back(center + radius * glm::vec2(std::cos(a), std::sin(a)));
  }
}

void Painter2D::QuadraticCurveTo(glm::vec2 control, glm::vec2 p) {
  auto& path = Current();
  const glm::vec2 start = path.points.empty() ? control : path.points.back();
  for (int i = 1; i <= 16; i++) {
    const float t = i / 16.0f;
    path.points.push_back((1 - t) * (1 - t) * start + 2 * (1 - t) * t * control +
                          t * t * p);
  }
}

void Painter2D::BezierCurveTo(glm::vec2 c1, glm::vec2 c2, glm::vec2 p) {
  auto& path = Current();
  const glm::vec2 start = path.points.empty() ? c1 : path.points.back();
  for (int i = 1; i <= 20; i++) {
    const float t = i / 20.0f, u = 1 - t;
    path.points.push_back(u * u * u * start + 3 * u * u * t * c1 +
                          3 * u * t * t * c2 + t * t * t * p);
  }
}

void Painter2D::ClosePath() {
  if (!paths_.empty()) paths_.back().closed = true;
}

void Painter2D::Fill() {
  if (!builder_) return;
  for (const auto& path : paths_) {
    std::vector<glm::vec2> px;
    for (const auto& p : path.points) px.push_back(Apply(transform_, p));
    builder_->FillPolygon(px, Pack(fillColor, opacity_));
  }
}

void Painter2D::Stroke() {
  if (!builder_) return;
  // Line width scales with the transform (approximately, by its x axis).
  const float scale = glm::length(glm::vec2(transform_[0]));
  for (const auto& path : paths_) {
    std::vector<glm::vec2> px;
    for (const auto& p : path.points) {
      glm::vec2 q = Apply(transform_, p);
      if (px.empty() || glm::distance(px.back(), q) > 0.01f) px.push_back(q);
    }
    builder_->Stroke(px, lineWidth * scale, Pack(strokeColor, opacity_), path.closed);
  }
}

// ---------------------------------------------------- MeshGenerationContext

MeshGenerationContext::MeshGenerationContext(MeshBuilder& builder,
                                             VisualElement* element,
                                             const glm::mat3& to_pixels,
                                             float opacity)
    : builder_(builder), element_(element), to_pixels_(to_pixels), opacity_(opacity) {
  painter_.builder_ = &builder;
  painter_.transform_ = to_pixels;
  painter_.opacity_ = opacity;
}

glm::vec2 MeshGenerationContext::ToPixels(glm::vec2 local) const {
  return Apply(to_pixels_, local);
}

void MeshGenerationContext::DrawRect(const Rect& r, Color color) {
  builder_.FillConvex({ToPixels({r.x, r.y}), ToPixels({r.xMax(), r.y}),
                       ToPixels({r.xMax(), r.yMax()}), ToPixels({r.x, r.yMax()})},
                      Pack(color, opacity_));
}

void MeshGenerationContext::DrawRoundedRect(const Rect& r,
                                            const std::array<float, 4>& radii,
                                            Color color) {
  const float scale = glm::length(glm::vec2(to_pixels_[0]));
  std::array<glm::vec2, 4> rr;
  std::array<int, 4> seg;
  for (int i = 0; i < 4; i++) {
    rr[i] = glm::vec2(radii[i]);
    seg[i] = CornerSegments(radii[i] * scale);
  }
  std::vector<glm::vec2> points;
  BuildRoundedContour(r, rr, seg, points, nullptr);
  for (auto& p : points) p = ToPixels(p);
  builder_.FillConvex(points, Pack(color, opacity_));
}

void MeshGenerationContext::DrawImage(const Rect& r, uint32_t texture_id,
                                      glm::vec4 uv, Color tint) {
  builder_.AddQuad({ToPixels({r.x, r.y}), ToPixels({r.xMax(), r.y}),
                    ToPixels({r.xMax(), r.yMax()}), ToPixels({r.x, r.yMax()})},
                   uv, Pack(tint, opacity_), texture_id);
}

void MeshGenerationContext::DrawText(const TextLayout& layout, const Font& font,
                                     glm::vec2 origin, const ComputedStyle& style) {
  const float pixel_scale = glm::length(glm::vec2(to_pixels_[0]));
  const float outline = style.unityTextOutlineWidth;
  const uint32_t outline_color = Pack(style.unityTextOutlineColor, opacity_);
  const uint32_t texture = font.atlasId();

  auto emit = [&](const PositionedGlyph& g, glm::vec2 offset, uint32_t color,
                  float softness, uint32_t outline_packed, float outline_width) {
    const glm::vec2 p = origin + g.position + offset;
    const float skew = g.italic ? g.size.y * 0.2f : 0.0f;
    // SDF units per pixel of this glyph: the full padding is 0.5 units.
    const float glyph_scale = g.size.y > 0 ? g.size.y / std::max(1.0f, (g.uv.w - g.uv.y) *
                                                                         font.atlas().height)
                                           : 1.0f;
    const float units_per_px = 0.5f / (kSdfPadding * glyph_scale);
    const float edge = (g.bold ? 0.43f : 0.5f);
    glm::vec4 params(0.0f, edge, outline_width * units_per_px, softness * units_per_px);
    builder_.AddQuad({ToPixels({p.x + skew, p.y}), ToPixels({p.x + g.size.x + skew, p.y}),
                      ToPixels({p.x + g.size.x, p.y + g.size.y}),
                      ToPixels({p.x, p.y + g.size.y})},
                     g.uv, color, texture, VertexMode::kText, params, outline_packed);
  };
  (void)pixel_scale;

  const TextShadow& shadow = style.textShadow;
  if (shadow.color.rgba.a > 0.0f &&
      (shadow.offset != glm::vec2(0.0f) || shadow.blur_radius > 0.0f)) {
    for (const auto& g : layout.glyphs) {
      Color c = shadow.color;
      c.rgba.a *= g.color.rgba.a;
      emit(g, shadow.offset, Pack(c, opacity_), shadow.blur_radius, 0, 0.0f);
    }
  }
  for (const auto& g : layout.glyphs) {
    emit(g, glm::vec2(0.0f), Pack(g.color, opacity_), 0.0f,
         outline > 0 ? outline_color : 0, outline);
  }
}

// ------------------------------------------------------------- Contours ---

void BuildRoundedContour(const Rect& r, const std::array<glm::vec2, 4>& radii,
                         const std::array<int, 4>& segments,
                         std::vector<glm::vec2>& points, std::vector<int>* sides) {
  points.clear();
  if (sides) sides->clear();
  const glm::vec2 centers[4] = {
      {r.x + radii[0].x, r.y + radii[0].y},
      {r.xMax() - radii[1].x, r.y + radii[1].y},
      {r.xMax() - radii[2].x, r.yMax() - radii[2].y},
      {r.x + radii[3].x, r.yMax() - radii[3].y},
  };
  // Each corner spans 90 degrees; the first half belongs to the side before
  // it, the second half to the side after it.
  const float start_angles[4] = {180.0f, 270.0f, 0.0f, 90.0f};
  const int side_before[4] = {0, 1, 2, 3};
  const int side_after[4] = {1, 2, 3, 0};
  for (int c = 0; c < 4; c++) {
    const int n = segments[c];
    for (int k = 0; k <= n; k++) {
      const float a = glm::radians(start_angles[c] + 90.0f * (n == 0 ? 0.5f : float(k) / n));
      const glm::vec2 p = centers[c] + glm::vec2(std::cos(a) * radii[c].x,
                                                 std::sin(a) * radii[c].y);
      const int mid = n / 2;
      if (k < mid || (n == 0)) {
        points.push_back(p);
        if (sides) sides->push_back(side_before[c]);
        if (n == 0) {
          points.push_back(p);
          if (sides) sides->push_back(side_after[c]);
        }
      } else if (k == mid) {
        points.push_back(p);
        if (sides) sides->push_back(side_before[c]);
        points.push_back(p);
        if (sides) sides->push_back(side_after[c]);
      } else {
        points.push_back(p);
        if (sides) sides->push_back(side_after[c]);
      }
    }
  }
}

namespace {

// Corner radii clamped so adjacent corners never overlap (CSS rules).
std::array<float, 4> ClampedRadii(const ComputedStyle& s, float w, float h) {
  std::array<float, 4> r = {
      s.borderTopLeftRadius.Resolve(w), s.borderTopRightRadius.Resolve(w),
      s.borderBottomRightRadius.Resolve(w), s.borderBottomLeftRadius.Resolve(w)};
  float f = 1.0f;
  auto fit = [&](float a, float b, float side) {
    if (a + b > side && a + b > 0) f = std::min(f, side / (a + b));
  };
  fit(r[0], r[1], w);
  fit(r[3], r[2], w);
  fit(r[0], r[3], h);
  fit(r[1], r[2], h);
  for (auto& v : r) v = std::max(0.0f, v * f);
  return r;
}

void DrawBackgroundImage(MeshGenerationContext& ctx, const VisualElement& e,
                         const Rect& box) {
  const ComputedStyle& s = e.resolvedStyle();
  const Background& bg = s.backgroundImage;
  if (!bg.IsSet() || !e.panel()) return;
  std::shared_ptr<UITexture> texture;
  if (bg.texture_id) {
    texture = UITextureRegistry::Instance().Find(bg.texture_id);
  } else {
    texture = e.panel()->resources().LoadTexture(bg.path);
  }
  if (!texture || texture->width == 0 || texture->height == 0) return;
  const Color tint = s.unityBackgroundImageTintColor;
  const float tw = static_cast<float>(texture->width);
  const float th = static_cast<float>(texture->height);

  const bool sliced = s.unitySliceLeft > 0 || s.unitySliceTop > 0 ||
                      s.unitySliceRight > 0 || s.unitySliceBottom > 0;
  if (sliced) {
    // 9-slice: corners keep their size (times -unity-slice-scale).
    const float k = s.unitySliceScale;
    float xs[4] = {box.x, box.x + s.unitySliceLeft * k,
                   box.xMax() - s.unitySliceRight * k, box.xMax()};
    float ys[4] = {box.y, box.y + s.unitySliceTop * k,
                   box.yMax() - s.unitySliceBottom * k, box.yMax()};
    if (xs[1] > xs[2]) xs[1] = xs[2] = (xs[1] + xs[2]) / 2;
    if (ys[1] > ys[2]) ys[1] = ys[2] = (ys[1] + ys[2]) / 2;
    const float us[4] = {0, s.unitySliceLeft / tw, 1 - s.unitySliceRight / tw, 1};
    const float vs[4] = {0, s.unitySliceTop / th, 1 - s.unitySliceBottom / th, 1};
    for (int y = 0; y < 3; y++) {
      for (int x = 0; x < 3; x++) {
        const Rect cell{xs[x], ys[y], xs[x + 1] - xs[x], ys[y + 1] - ys[y]};
        if (cell.width <= 0 || cell.height <= 0) continue;
        ctx.DrawImage(cell, texture->id, {us[x], vs[y], us[x + 1], vs[y + 1]}, tint);
      }
    }
    return;
  }

  Rect rect = box;
  glm::vec4 uv(0, 0, 1, 1);
  const float box_aspect = box.height > 0 ? box.width / box.height : 1.0f;
  const float tex_aspect = tw / th;
  switch (s.unityBackgroundScaleMode) {
    case ScaleMode::kStretchToFill: break;
    case ScaleMode::kScaleToFit:
      if (tex_aspect > box_aspect) {
        rect.height = box.width / tex_aspect;
        rect.y += (box.height - rect.height) / 2;
      } else {
        rect.width = box.height * tex_aspect;
        rect.x += (box.width - rect.width) / 2;
      }
      break;
    case ScaleMode::kScaleAndCrop:
      if (tex_aspect > box_aspect) {
        const float visible = box_aspect / tex_aspect;
        uv.x = (1 - visible) / 2;
        uv.z = uv.x + visible;
      } else {
        const float visible = tex_aspect / box_aspect;
        uv.y = (1 - visible) / 2;
        uv.w = uv.y + visible;
      }
      break;
  }
  ctx.DrawImage(rect, texture->id, uv, tint);
}

}  // namespace

void DrawElementBox(MeshGenerationContext& ctx, const VisualElement& e) {
  const ComputedStyle& s = e.resolvedStyle();
  const Rect& layout = e.layout();
  const float w = layout.width, h = layout.height;
  if (w <= 0 || h <= 0) return;
  const Rect box{0, 0, w, h};
  const auto radii = ClampedRadii(s, w, h);
  const float pixel_scale = glm::length(glm::vec2(ctx.transform()[0]));
  const float bl = s.borderLeftWidth, bt = s.borderTopWidth;
  const float br = s.borderRightWidth, bb = s.borderBottomWidth;
  const bool has_border =
      (bl > 0 && s.borderLeftColor.rgba.a > 0) || (bt > 0 && s.borderTopColor.rgba.a > 0) ||
      (br > 0 && s.borderRightColor.rgba.a > 0) || (bb > 0 && s.borderBottomColor.rgba.a > 0);
  const bool rounded = radii[0] > 0 || radii[1] > 0 || radii[2] > 0 || radii[3] > 0;

  std::array<int, 4> segments;
  std::array<glm::vec2, 4> outer_radii;
  for (int i = 0; i < 4; i++) {
    segments[i] = CornerSegments(radii[i] * pixel_scale);
    outer_radii[i] = glm::vec2(radii[i]);
  }

  if (s.backgroundColor.rgba.a > 0) {
    std::vector<glm::vec2> contour;
    BuildRoundedContour(box, outer_radii, segments, contour, nullptr);
    for (auto& p : contour) p = ctx.ToPixels(p);
    ctx.builder().FillConvex(contour, Pack(s.backgroundColor, ctx.opacity()));
  }

  DrawBackgroundImage(ctx, e, Rect{bl, bt, std::max(0.0f, w - bl - br),
                                   std::max(0.0f, h - bt - bb)});

  if (has_border) {
    std::vector<glm::vec2> outer, inner;
    std::vector<int> sides;
    BuildRoundedContour(box, outer_radii, segments, outer, &sides);
    const Rect inner_box{bl, bt, std::max(0.0f, w - bl - br), std::max(0.0f, h - bt - bb)};
    const std::array<glm::vec2, 4> inner_radii = {
        glm::max(glm::vec2(radii[0] - bl, radii[0] - bt), glm::vec2(0.0f)),
        glm::max(glm::vec2(radii[1] - br, radii[1] - bt), glm::vec2(0.0f)),
        glm::max(glm::vec2(radii[2] - br, radii[2] - bb), glm::vec2(0.0f)),
        glm::max(glm::vec2(radii[3] - bl, radii[3] - bb), glm::vec2(0.0f)),
    };
    BuildRoundedContour(inner_box, inner_radii, segments, inner, nullptr);
    const uint32_t side_colors[4] = {
        bl > 0 ? Pack(s.borderLeftColor, ctx.opacity()) : 0u,
        bt > 0 ? Pack(s.borderTopColor, ctx.opacity()) : 0u,
        br > 0 ? Pack(s.borderRightColor, ctx.opacity()) : 0u,
        bb > 0 ? Pack(s.borderBottomColor, ctx.opacity()) : 0u,
    };
    std::vector<uint32_t> colors;
    colors.reserve(sides.size());
    for (int side : sides) colors.push_back(side_colors[side]);
    for (auto& p : outer) p = ctx.ToPixels(p);
    for (auto& p : inner) p = ctx.ToPixels(p);
    ctx.builder().FillRing(outer, inner, colors);
  }
  (void)rounded;
}

}  // namespace Mercury::UI
