// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * runtime_dump.h
 *
 * A snapshot of a live MapScene, for the input script's `dump` command and
 * tools that step a game headlessly. Unlike the map/asset inspectors this
 * reads the running scene: script fields, physics velocities and the world
 * transforms after this frame's simulation, not the values saved on disk.
 *
 * Like the rest of introspection, the public surface is plain std::string;
 * nlohmann/json stays private to runtime_dump.cc.
 */
#ifndef MERCURY_INTROSPECTION_RUNTIME_DUMP_H
#define MERCURY_INTROSPECTION_RUNTIME_DUMP_H

#include <string>
#include <vector>

namespace Mercury {

class MapScene;

namespace introspection {

/**
 * @brief Serialize the current state of `scene` as JSON
 *
 * Returns {"nodes": [...], "errors": [...]}. Each node carries its path,
 * uid (when it has one), active state, tag, layer, world-space transform
 * (rotation as Euler degrees, like GameObject.eulerAngles), local position,
 * rigidbody velocities when it has one, and the fields of every component
 * TypeRegistry says it has. Unknown paths are reported in "errors"; the
 * remaining nodes are still dumped. An empty `nodes` means every node.
 */
std::string DumpSceneJson(MapScene& scene,
                          const std::vector<std::string>& nodes,
                          std::string* error = nullptr);

}  // namespace introspection
}  // namespace Mercury

#endif  // MERCURY_INTROSPECTION_RUNTIME_DUMP_H
