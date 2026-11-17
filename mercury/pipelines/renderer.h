// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/**
 * renderer.h
 *
 * The goal of this file is to fit in an easily abstractable pipeline that takes
 * care of all the rendering logic
 */
#ifndef MERCURY_ENGINE_RENDERER_H
#define MERCURY_ENGINE_RENDERER_H

#include <cstdint>

#include <vector>

typedef struct VertexBuffer {
} VertexBuffer;

using IndexBuffer = std::vector<uint32_t>;

struct Shader {};

/**
 * @brief This should be everything a renderer needs to complete a render
 */
typedef struct RendereringObject {
  // Buffers
  std::vector<VertexBuffer> vertex_buffers_;
  std::vector<IndexBuffer> index_buffers_;

  unsigned int buffers_ = 0;
} RendereringObject;

namespace Mercury {

class RendererPipeline {
 public:
  RendererPipeline();
  RendereringObject* GetRenderingObject();

  void ContinuePass();
};

}  // namespace Mercury

#endif  // MERCURY_ENGINE_RENDERER_H
