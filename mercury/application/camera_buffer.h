// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H
#define MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H

#include "globals.h"

namespace Mercury {

// Matches VS_CAMERA_BUFFER in main.vs.hlsl / static.vs.hlsl.
struct CameraCBuffer {
  PQ_MATRIX mWorldViewProj;
};
static_assert(sizeof(CameraCBuffer) == 64);

}  // namespace Mercury

#endif  // MERCURYENGINE_APPLICATION_CAMERA_BUFFER_H
