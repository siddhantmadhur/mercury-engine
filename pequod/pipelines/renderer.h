/**
 * renderer.h
 *
 * The goal of this file is to fit in an easily abstractable pipeline that takes
 * care of all the rendering logic
 */
#ifndef PEQUOD_ENGINE_RENDERER_H
#define PEQUOD_ENGINE_RENDERER_H

#include <intsafe.h>

#include <vector>

typedef struct VertexBuffer {
} VertexBuffer;

using IndexBuffer = std::vector<UINT>;

typedef struct Shader {};

/**
 * @brief This should be everything a renderer needs to complete a render
 */
typedef struct RendereringObject {
  // Buffers
  std::vector<VertexBuffer> vertex_buffers_;
  std::vector<IndexBuffer> index_buffers_;

  unsigned int buffers_ = 0;
} RendereringObject;

namespace Pequod {

class RendererPipeline {
 public:
  RendererPipeline();
  RendereringObject* GetRenderingObject();

  void ContinuePass();
};

}  // namespace Pequod

#endif  // PEQUOD_ENGINE_RENDERER_H
