/*
 * ui_texture.h
 *
 * CPU-side images used by the UI (loaded pictures, font atlases, textures
 * made in code). Renderers look textures up by id and upload one again
 * whenever its version changes.
 */
#ifndef MERCURY_UI_UI_TEXTURE_H
#define MERCURY_UI_UI_TEXTURE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mercury::UI {

struct UITexture {
  uint32_t id = 0;
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4, sRGB
  uint64_t version = 1;       // bump after changing pixels
  bool linear_filter = true;  // false for pixel art (nearest)

  void MarkChanged() { version++; }
};

/**
 * @brief Every live UI texture, by id
 *
 * Shared by all panels in the process so a renderer has one place to look.
 */
class UITextureRegistry {
 public:
  static UITextureRegistry& Instance();

  // Creates a texture; it lives until Release(id).
  std::shared_ptr<UITexture> Create(int width, int height,
                                    std::vector<uint8_t> rgba = {});
  // Loads (once) an image file; nullptr if it can't be read.
  std::shared_ptr<UITexture> LoadFile(const std::string& absolute_path);
  void Release(uint32_t id);

  std::shared_ptr<UITexture> Find(uint32_t id) const;
  // Increases whenever a texture is released, so renderers know to look
  // for textures they can free.
  uint64_t generation() const { return generation_; }

 private:
  UITextureRegistry() = default;
  mutable std::mutex mutex_;
  uint32_t next_id_ = 1;
  uint64_t generation_ = 0;
  std::unordered_map<uint32_t, std::shared_ptr<UITexture>> textures_;
  std::unordered_map<std::string, uint32_t> by_path_;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_UI_TEXTURE_H
