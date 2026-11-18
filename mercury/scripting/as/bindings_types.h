// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * bindings_types.h
 *
 * Small value types shared by the native bindings.
 */
#ifndef MERCURY_SCRIPTING_AS_BINDINGS_TYPES_H
#define MERCURY_SCRIPTING_AS_BINDINGS_TYPES_H

#include <string>

#include "scene/map_scene.h"

namespace Mercury {

// A component on a GameObject. `type` is the TypeRegistry name, so the value
// stays valid even when the registry grows.
struct ScriptComponent {
  GameObject object;
  std::string type;
};

// One action in the project's Input Actions asset.
struct ScriptInputAction {
  int index = -1;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_BINDINGS_TYPES_H
