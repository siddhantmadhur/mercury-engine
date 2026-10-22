/*
 * font.h
 *
 * TrueType fonts drawn from a signed distance field atlas, the way Unity's
 * TextCore / TextMeshPro do it: each glyph is rasterized once at a base size
 * and stays sharp at any size, with outlines and bold for free.
 *
 * Also lays out text: kerning, word wrap, alignment, ellipsis and Unity's
 * basic rich text tags (<b>, <i>, <color=#hex>, <size=N>, <alpha=#hex>, <br>).
 */
#ifndef MERCURY_UI_FONT_H
#define MERCURY_UI_FONT_H

#include <cstdint>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/style.h"
#include "ui/ui_texture.h"

struct stbtt_fontinfo;

namespace Mercury::UI {

constexpr float kSdfBaseSize = 48.0f;  // pixel size glyphs are rasterized at
constexpr int kSdfPadding = 6;         // distance range around each glyph

struct GlyphInfo {
  float advance = 0.0f;       // base-size pixels
  glm::vec2 offset{0.0f};     // bitmap top-left from the pen, base pixels
  glm::vec2 size{0.0f};       // bitmap size, base pixels
  glm::ivec4 atlas_rect{0};   // x, y, w, h in the atlas
  bool visible = false;       // false for spaces
};

class Font {
 public:
  ~Font();
  static std::shared_ptr<Font> LoadFile(const std::string& path);
  static std::shared_ptr<Font> FromMemory(std::vector<uint8_t> data);

  // Rasterizes the glyph into the atlas the first time it is asked for.
  const GlyphInfo& Glyph(char32_t codepoint);
  float Kerning(char32_t left, char32_t right) const;  // base pixels

  // Metrics in base-size pixels; descent is negative.
  float ascent() const { return ascent_; }
  float descent() const { return descent_; }
  float lineGap() const { return line_gap_; }
  float lineHeight() const { return ascent_ - descent_ + line_gap_; }

  const UITexture& atlas() const { return *atlas_; }
  uint32_t atlasId() const { return atlas_->id; }
  const std::string& path() const { return path_; }

  // Counts atlas resizes across all fonts; glyph UVs laid out before a
  // resize are stale, so panels repaint when this changes.
  static uint64_t AtlasResizeCount();

 private:
  Font() = default;
  bool Init(std::vector<uint8_t> data);
  bool Pack(int w, int h, glm::ivec2& out);

  std::vector<uint8_t> data_;
  std::unique_ptr<stbtt_fontinfo> info_;
  float scale_ = 1.0f;
  float ascent_ = 0.0f, descent_ = 0.0f, line_gap_ = 0.0f;
  std::shared_ptr<UITexture> atlas_;
  std::unordered_map<char32_t, GlyphInfo> glyphs_;
  // Shelf packer state.
  int shelf_x_ = 1, shelf_y_ = 1, shelf_height_ = 0;
  std::string path_;
};

struct TextSettings {
  Font* font = nullptr;
  float size = 12.0f;
  Color color = Color::Black();
  FontStyle font_style = FontStyle::kNormal;
  TextAnchor align = TextAnchor::kUpperLeft;
  WhiteSpace white_space = WhiteSpace::kNormal;
  TextOverflow overflow = TextOverflow::kClip;
  float letter_spacing = 0.0f;
  float word_spacing = 0.0f;
  float paragraph_spacing = 0.0f;
  bool rich_text = true;
};

struct PositionedGlyph {
  char32_t codepoint = 0;
  glm::vec2 position{0.0f};  // quad top-left
  glm::vec2 size{0.0f};
  glm::vec4 uv{0.0f};        // u0, v0, u1, v1
  Color color;
  bool bold = false;
  bool italic = false;
};

struct TextLayout {
  std::vector<PositionedGlyph> glyphs;
  glm::vec2 size{0.0f};  // of the laid out text
  float line_height = 0.0f;
  // Caret position (top of the line) before each codepoint of the source
  // text without tags, plus one past the end. Only filled without rich text.
  std::vector<glm::vec2> carets;
  std::vector<int> caret_lines;
};

/**
 * @brief Lays text out inside a box of `box_width`
 *
 * Wraps at `box_width` unless white-space is nowrap/pre; an infinite width
 * never wraps. Horizontal alignment uses the box width when finite.
 */
TextLayout LayoutText(std::string_view utf8, const TextSettings& settings,
                      float box_width = std::numeric_limits<float>::infinity());

glm::vec2 MeasureText(std::string_view utf8, const TextSettings& settings,
                      float max_width = std::numeric_limits<float>::infinity());

// UTF-8 helpers.
std::u32string DecodeUtf8(std::string_view utf8);
std::string EncodeUtf8(std::u32string_view text);
void AppendUtf8(std::string& out, char32_t codepoint);
// Removes rich text tags (for measuring cursors in plain text).
std::string StripRichText(std::string_view utf8);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_FONT_H
