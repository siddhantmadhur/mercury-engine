// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * filament_ui.h
 *
 * Draws UI Toolkit draw lists (Mercury::UI::UIDrawData) with Filament: one
 * scene with a renderable per command, its vertices re-uploaded every
 * frame, and every UITexture mirrored into a Filament texture. The view
 * is rendered on top of the scene view (the caller sets the blend mode).
 */
#ifndef MERCURY_ENGINE_FILAMENT_FILAMENT_UI_H
#define MERCURY_ENGINE_FILAMENT_FILAMENT_UI_H

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <utils/Entity.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ui/draw_data.h"

namespace Mercury::fil {

class FilamentUiRenderer {
 public:
  FilamentUiRenderer() = default;
  ~FilamentUiRenderer() { /* Shutdown() must have been called */ }

  bool Init(filament::Engine* engine);
  void Shutdown(filament::Engine* engine);

  // Uploads `data` and rebuilds the renderables. Safe to call with an
  // empty list (everything is removed).
  void Prepare(filament::Engine* engine, const UI::UIDrawData& data);

  // Renders into the caller's output; attach it to a View of your own
  // with BlendMode::TRANSLUCENT to draw it over the scene.
  filament::Scene* scene() const { return scene_; }

 private:
  struct TextureEntry {
    filament::Texture* texture = nullptr;
    uint64_t version = 0;
  };

  filament::Texture* SyncTexture(filament::Engine* engine, uint32_t id);
  void DestroyRenderables(filament::Engine* engine);

  // Textures retired because a new version replaced them; freed once the
  // frames still sampling them are done.
  struct Garbage {
    uint64_t frame = 0;
    filament::Texture* texture = nullptr;
  };

  filament::Scene* scene_ = nullptr;
  filament::Material* material_ = nullptr;
  filament::Texture* white_ = nullptr;  // the "no texture" 1x1 white

  filament::VertexBuffer* vertices_ = nullptr;
  filament::IndexBuffer* indices_ = nullptr;
  uint32_t vertex_capacity_ = 0;
  uint32_t index_capacity_ = 0;

  std::vector<utils::Entity> renderables_;
  std::unordered_map<uint32_t, TextureEntry> textures_;
  std::vector<Garbage> garbage_;
  uint64_t frame_ = 0;
};

}  // namespace Mercury::fil

#endif  // MERCURY_ENGINE_FILAMENT_FILAMENT_UI_H
