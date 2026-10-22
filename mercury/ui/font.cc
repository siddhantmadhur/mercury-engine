#include "font.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "debugger/debugger.h"

// Our own copy: Dear ImGui compiles its stb_truetype as static functions.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

#include "ui/style_sheet.h"

namespace Mercury::UI {

namespace {

constexpr int kAtlasWidth = 1024;
constexpr int kInitialAtlasHeight = 512;
constexpr int kMaxAtlasHeight = 4096;

uint64_t g_atlas_resizes = 0;

}  // namespace

uint64_t Font::AtlasResizeCount() { return g_atlas_resizes; }

Font::~Font() {
  if (atlas_) UITextureRegistry::Instance().Release(atlas_->id);
}

std::shared_ptr<Font> Font::LoadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    PDebug::warn("UI: could not open font {}", path);
    return nullptr;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
  auto font = FromMemory(std::move(data));
  if (font) font->path_ = path;
  else PDebug::warn("UI: {} is not a TrueType font", path);
  return font;
}

std::shared_ptr<Font> Font::FromMemory(std::vector<uint8_t> data) {
  std::shared_ptr<Font> font(new Font());
  if (!font->Init(std::move(data))) return nullptr;
  return font;
}

bool Font::Init(std::vector<uint8_t> data) {
  data_ = std::move(data);
  info_ = std::make_unique<stbtt_fontinfo>();
  const int offset = stbtt_GetFontOffsetForIndex(data_.data(), 0);
  if (offset < 0 || !stbtt_InitFont(info_.get(), data_.data(), offset)) {
    return false;
  }
  scale_ = stbtt_ScaleForPixelHeight(info_.get(), kSdfBaseSize);
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(info_.get(), &ascent, &descent, &gap);
  ascent_ = ascent * scale_;
  descent_ = descent * scale_;
  line_gap_ = gap * scale_;
  atlas_ = UITextureRegistry::Instance().Create(kAtlasWidth, kInitialAtlasHeight);
  // White everywhere; the distance lives in alpha.
  for (size_t i = 0; i < atlas_->rgba.size(); i += 4) {
    atlas_->rgba[i] = atlas_->rgba[i + 1] = atlas_->rgba[i + 2] = 255;
    atlas_->rgba[i + 3] = 0;
  }
  return true;
}

bool Font::Pack(int w, int h, glm::ivec2& out) {
  if (shelf_x_ + w + 1 > atlas_->width) {
    shelf_x_ = 1;
    shelf_y_ += shelf_height_ + 1;
    shelf_height_ = 0;
  }
  while (shelf_y_ + h + 1 > atlas_->height) {
    if (atlas_->height >= kMaxAtlasHeight) return false;
    // Growing downwards keeps every packed glyph where it is.
    const int new_height = atlas_->height * 2;
    std::vector<uint8_t> grown(static_cast<size_t>(atlas_->width) * new_height * 4);
    for (size_t i = 0; i < grown.size(); i += 4) {
      grown[i] = grown[i + 1] = grown[i + 2] = 255;
      grown[i + 3] = 0;
    }
    std::copy(atlas_->rgba.begin(), atlas_->rgba.end(), grown.begin());
    atlas_->rgba = std::move(grown);
    atlas_->height = new_height;
    g_atlas_resizes++;
  }
  out = {shelf_x_, shelf_y_};
  shelf_x_ += w + 1;
  shelf_height_ = std::max(shelf_height_, h);
  return true;
}

const GlyphInfo& Font::Glyph(char32_t codepoint) {
  auto it = glyphs_.find(codepoint);
  if (it != glyphs_.end()) return it->second;

  GlyphInfo glyph;
  int glyph_index = stbtt_FindGlyphIndex(info_.get(), static_cast<int>(codepoint));
  if (glyph_index == 0 && codepoint != 0xFFFD && codepoint > 32) {
    // Missing glyph: show the replacement character (or '?').
    const GlyphInfo& fallback = Glyph(
        stbtt_FindGlyphIndex(info_.get(), 0xFFFD) ? char32_t(0xFFFD) : char32_t('?'));
    return glyphs_.emplace(codepoint, fallback).first->second;
  }
  int advance = 0, lsb = 0;
  stbtt_GetGlyphHMetrics(info_.get(), glyph_index, &advance, &lsb);
  glyph.advance = advance * scale_;

  int w = 0, h = 0, xoff = 0, yoff = 0;
  unsigned char* sdf = stbtt_GetGlyphSDF(info_.get(), scale_, glyph_index,
                                         kSdfPadding, 128,
                                         128.0f / kSdfPadding, &w, &h, &xoff, &yoff);
  if (sdf && w > 0 && h > 0) {
    glm::ivec2 at;
    if (Pack(w, h, at)) {
      for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
          const size_t dst =
              (static_cast<size_t>(at.y + y) * atlas_->width + at.x + x) * 4;
          atlas_->rgba[dst + 3] = sdf[y * w + x];
        }
      }
      atlas_->MarkChanged();
      glyph.atlas_rect = {at.x, at.y, w, h};
      glyph.offset = {static_cast<float>(xoff), static_cast<float>(yoff)};
      glyph.size = {static_cast<float>(w), static_cast<float>(h)};
      glyph.visible = true;
    } else {
      PDebug::warn("UI: font atlas is full");
    }
  }
  if (sdf) stbtt_FreeSDF(sdf, nullptr);
  return glyphs_.emplace(codepoint, glyph).first->second;
}

float Font::Kerning(char32_t left, char32_t right) const {
  return stbtt_GetCodepointKernAdvance(info_.get(), static_cast<int>(left),
                                       static_cast<int>(right)) *
         scale_;
}

// ------------------------------------------------------------- UTF-8 -----

void AppendUtf8(std::string& out, char32_t c) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

std::u32string DecodeUtf8(std::string_view s) {
  std::u32string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    char32_t cp = 0xFFFD;
    int extra = 0;
    if (c < 0x80) {
      cp = c;
    } else if ((c >> 5) == 0x6) {
      cp = c & 0x1F;
      extra = 1;
    } else if ((c >> 4) == 0xE) {
      cp = c & 0x0F;
      extra = 2;
    } else if ((c >> 3) == 0x1E) {
      cp = c & 0x07;
      extra = 3;
    }
    i++;
    for (int k = 0; k < extra && i < s.size(); k++, i++) {
      cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    }
    out.push_back(cp);
  }
  return out;
}

std::string EncodeUtf8(std::u32string_view text) {
  std::string out;
  for (char32_t c : text) AppendUtf8(out, c);
  return out;
}

namespace {

struct StyledChar {
  char32_t c;
  Color color;
  float size;
  bool bold;
  bool italic;
};

// Splits text into characters with their rich text styling applied.
std::vector<StyledChar> ApplyRichText(std::u32string_view text,
                                      const TextSettings& settings) {
  std::vector<StyledChar> out;
  std::vector<Color> colors{settings.color};
  std::vector<float> sizes{settings.size};
  std::vector<float> alphas{settings.color.rgba.a};
  int bold = (settings.font_style == FontStyle::kBold ||
              settings.font_style == FontStyle::kBoldAndItalic)
                 ? 1
                 : 0;
  int italic = (settings.font_style == FontStyle::kItalic ||
                settings.font_style == FontStyle::kBoldAndItalic)
                   ? 1
                   : 0;
  for (size_t i = 0; i < text.size(); i++) {
    if (settings.rich_text && text[i] == '<') {
      const size_t close = text.find('>', i);
      if (close != std::u32string_view::npos) {
        std::string tag = EncodeUtf8(text.substr(i + 1, close - i - 1));
        std::string lower = tag;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        bool handled = true;
        auto value = [&]() {
          const size_t eq = tag.find('=');
          std::string v = eq == std::string::npos ? "" : tag.substr(eq + 1);
          if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) {
            v = v.substr(1, v.size() - 2);
          }
          return v;
        };
        if (lower == "b") bold++;
        else if (lower == "/b") bold = std::max(0, bold - 1);
        else if (lower == "i") italic++;
        else if (lower == "/i") italic = std::max(0, italic - 1);
        else if (lower == "br" || lower == "br/") {
          out.push_back({'\n', colors.back(), sizes.back(), bold > 0, italic > 0});
        } else if (lower.starts_with("color=")) {
          auto c = ParseColor(value());
          colors.push_back(c ? *c : colors.back());
        } else if (lower == "/color") {
          if (colors.size() > 1) colors.pop_back();
        } else if (lower.starts_with("size=")) {
          std::string v = value();
          float size = sizes.back();
          if (v.ends_with('%')) {
            size = settings.size * std::strtof(v.c_str(), nullptr) / 100.0f;
          } else {
            size = std::strtof(v.c_str(), nullptr);
          }
          sizes.push_back(size > 0 ? size : sizes.back());
        } else if (lower == "/size") {
          if (sizes.size() > 1) sizes.pop_back();
        } else if (lower.starts_with("alpha=")) {
          std::string v = value();
          if (v.starts_with('#')) v = v.substr(1);
          alphas.push_back(std::strtol(v.c_str(), nullptr, 16) / 255.0f);
        } else if (lower == "/alpha") {
          if (alphas.size() > 1) alphas.pop_back();
        } else {
          handled = false;
        }
        if (handled) {
          i = close;
          continue;
        }
      }
    }
    Color color = colors.back();
    if (alphas.size() > 1) color.rgba.a = alphas.back();
    out.push_back({text[i], color, sizes.back(), bold > 0, italic > 0});
  }
  return out;
}

bool IsSpace(char32_t c) { return c == ' ' || c == '\t'; }

}  // namespace

std::string StripRichText(std::string_view utf8) {
  TextSettings settings;
  std::string out;
  for (const auto& c : ApplyRichText(DecodeUtf8(utf8), settings)) {
    AppendUtf8(out, c.c);
  }
  return out;
}

TextLayout LayoutText(std::string_view utf8, const TextSettings& settings,
                      float box_width) {
  TextLayout layout;
  Font* font = settings.font;
  if (!font) return layout;
  const auto chars = ApplyRichText(DecodeUtf8(utf8), settings);
  const bool wrap = std::isfinite(box_width) &&
                    (settings.white_space == WhiteSpace::kNormal ||
                     settings.white_space == WhiteSpace::kPreWrap);
  const bool collapse = settings.white_space == WhiteSpace::kNormal ||
                        settings.white_space == WhiteSpace::kNoWrap;
  const float base_scale = settings.size / kSdfBaseSize;
  layout.line_height = font->lineHeight() * base_scale;

  struct Line {
    size_t first = 0, last = 0;  // glyph range [first, last)
    float width = 0.0f;
    float height = 0.0f;
    float ascent = 0.0f;
  };
  std::vector<Line> lines(1);
  // Pen positions per char, before alignment, relative to the line start.
  struct Placed {
    size_t char_index;
    float x;
    float advance;
    size_t line;
  };
  std::vector<Placed> placed;
  placed.reserve(chars.size());

  float pen = 0.0f;
  size_t last_break = std::string::npos;  // index in `placed` after a space
  char32_t prev = 0;
  bool prev_space = false;

  for (size_t i = 0; i < chars.size(); i++) {
    const StyledChar& sc = chars[i];
    const float scale = sc.size / kSdfBaseSize;
    if (sc.c == '\n') {
      placed.push_back({i, pen, 0.0f, lines.size() - 1});
      lines.back().width = pen;
      lines.emplace_back();
      pen = 0.0f;
      last_break = std::string::npos;
      prev = 0;
      prev_space = false;
      continue;
    }
    char32_t c = sc.c == '\t' ? char32_t(' ') : sc.c;
    if (collapse && IsSpace(c)) {
      if (prev_space || placed.empty() || pen == 0.0f) {
        // Collapsed: keep a caret entry with no width.
        placed.push_back({i, pen, 0.0f, lines.size() - 1});
        continue;
      }
    }
    const GlyphInfo& glyph = font->Glyph(c);
    float advance = glyph.advance * scale + settings.letter_spacing;
    if (c == ' ') advance += settings.word_spacing;
    if (prev) pen += font->Kerning(prev, c) * scale;

    if (wrap && !IsSpace(c) && pen + glyph.advance * scale > box_width &&
        pen > 0.0f) {
      // Move the current word to a new line.
      size_t move_from = last_break != std::string::npos ? last_break : placed.size();
      float shift = move_from < placed.size() ? placed[move_from].x : pen;
      // Width of the finished line excludes trailing spaces.
      float width = shift;
      for (size_t k = move_from; k-- > 0;) {
        if (placed[k].line != lines.size() - 1) break;
        if (!IsSpace(chars[placed[k].char_index].c)) break;
        width = placed[k].x;
      }
      lines.back().width = width;
      lines.emplace_back();
      for (size_t k = move_from; k < placed.size(); k++) {
        placed[k].x -= shift;
        placed[k].line = lines.size() - 1;
      }
      pen -= shift;
      last_break = std::string::npos;
    }

    placed.push_back({i, pen, advance, lines.size() - 1});
    pen += advance;
    if (IsSpace(c)) last_break = placed.size();
    prev_space = IsSpace(c);
    prev = c;
  }
  lines.back().width = pen;
  // Trailing spaces don't count toward the width of the last line.
  for (size_t k = placed.size(); k-- > 0;) {
    if (placed[k].line != lines.size() - 1) break;
    if (!IsSpace(chars[placed[k].char_index].c)) break;
    lines.back().width = placed[k].x;
  }

  // Line heights from the largest size used on each line.
  for (auto& line : lines) line.height = 0.0f;
  for (const auto& p : placed) {
    const float scale = chars[p.char_index].size / kSdfBaseSize;
    auto& line = lines[p.line];
    line.height = std::max(line.height, font->lineHeight() * scale);
    line.ascent = std::max(line.ascent, font->ascent() * scale);
  }
  for (auto& line : lines) {
    if (line.height == 0.0f) {
      line.height = layout.line_height;
      line.ascent = font->ascent() * base_scale;
    }
  }

  // Ellipsis for single-line text that doesn't fit.
  const bool ellipsis = settings.overflow == TextOverflow::kEllipsis &&
                        std::isfinite(box_width) && lines.size() == 1 &&
                        lines[0].width > box_width + 0.5f;
  size_t visible_count = placed.size();
  float ellipsis_x = 0.0f;
  if (ellipsis) {
    const GlyphInfo& dot = font->Glyph(U'…');
    const float dot_width = dot.advance * base_scale;
    while (visible_count > 0 &&
           placed[visible_count - 1].x + placed[visible_count - 1].advance +
                   dot_width >
               box_width) {
      visible_count--;
    }
    ellipsis_x = visible_count > 0
                     ? placed[visible_count - 1].x + placed[visible_count - 1].advance
                     : 0.0f;
    lines[0].width = ellipsis_x + dot_width;
  }

  // Alignment and final positions.
  float max_width = 0.0f;
  for (const auto& line : lines) max_width = std::max(max_width, line.width);
  const float align_width = std::isfinite(box_width) ? box_width : max_width;
  const int column = static_cast<int>(settings.align) % 3;
  std::vector<float> line_x(lines.size()), line_y(lines.size());
  float y = 0.0f;
  for (size_t l = 0; l < lines.size(); l++) {
    line_x[l] = column == 0 ? 0.0f
                : column == 1 ? (align_width - lines[l].width) / 2.0f
                              : align_width - lines[l].width;
    line_y[l] = y;
    y += lines[l].height;
    if (l + 1 < lines.size()) y += settings.paragraph_spacing;
  }
  layout.size = {max_width, y};

  const auto& atlas = font->atlas();
  auto emit = [&](char32_t c, const StyledChar& sc, float x, size_t line) {
    const GlyphInfo& glyph = font->Glyph(c);
    if (!glyph.visible) return;
    const float scale = sc.size / kSdfBaseSize;
    PositionedGlyph g;
    g.codepoint = c;
    const float baseline = line_y[line] + lines[line].ascent;
    g.position = {line_x[line] + x + glyph.offset.x * scale,
                  baseline + glyph.offset.y * scale};
    g.size = glyph.size * scale;
    g.uv = {glyph.atlas_rect.x / float(atlas.width),
            glyph.atlas_rect.y / float(atlas.height),
            (glyph.atlas_rect.x + glyph.atlas_rect.z) / float(atlas.width),
            (glyph.atlas_rect.y + glyph.atlas_rect.w) / float(atlas.height)};
    g.color = sc.color;
    g.bold = sc.bold;
    g.italic = sc.italic;
    layout.glyphs.push_back(g);
  };
  for (size_t k = 0; k < visible_count; k++) {
    const auto& p = placed[k];
    const StyledChar& sc = chars[p.char_index];
    if (sc.c == '\n' || IsSpace(sc.c)) continue;
    emit(sc.c, sc, p.x, p.line);
  }
  if (ellipsis && !chars.empty()) emit(U'…', chars[0], ellipsis_x, 0);

  if (!settings.rich_text) {
    layout.carets.reserve(chars.size() + 1);
    for (const auto& p : placed) {
      layout.carets.push_back({line_x[p.line] + p.x, line_y[p.line]});
      layout.caret_lines.push_back(static_cast<int>(p.line));
    }
    const size_t last_line = lines.size() - 1;
    float end_x = placed.empty() ? 0.0f : placed.back().x + placed.back().advance;
    if (!placed.empty() && chars[placed.back().char_index].c == '\n') end_x = 0.0f;
    layout.carets.push_back({line_x[last_line] + end_x, line_y[last_line]});
    layout.caret_lines.push_back(static_cast<int>(last_line));
  }
  return layout;
}

glm::vec2 MeasureText(std::string_view utf8, const TextSettings& settings,
                      float max_width) {
  TextSettings measure = settings;
  measure.align = TextAnchor::kUpperLeft;
  measure.overflow = TextOverflow::kClip;
  TextLayout layout = LayoutText(utf8, measure, max_width);
  glm::vec2 size = layout.size;
  if (utf8.empty() && settings.font) size.y = layout.line_height;
  return size;
}

}  // namespace Mercury::UI
