#ifndef MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H
#define MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H

#include <cstddef>
#include "globals.h"

namespace Mercury {

// Matches VS_CAMERA_BUFFER / CameraUniforms in shaders (std140-style: every float4
// starts on a 16-byte boundary).
struct CameraCBuffer {
  PQ_MATRIX mWorldViewProj;
  PQ_MATRIX mLightViewProj;
  PQ_FLOAT4 mLightDirection;  // xyz: the way the light travels
  PQ_FLOAT4 mLightColor;      // rgb: color * intensity
  PQ_FLOAT4 mAmbientColor;
  PQ_FLOAT4 mCameraPosition;
  PQ_FLOAT2 mResolution;
  PQ_FLOAT2 mShadowParams;    // x: shadow map resolution, y: normal bias
};
static_assert(offsetof(CameraCBuffer, mLightDirection) == 128);
static_assert(sizeof(CameraCBuffer) == 208);

}  // namespace Mercury

#endif  // MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H
