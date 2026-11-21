// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * variant_json.h
 *
 * INTERNAL header: it includes nlohmann/json, so it is excluded from the SDK
 * install (see the install(DIRECTORY ...) rules in CMakeLists.txt) and is only
 * for code that links nlohmann itself (the engine's own .cc files and the
 * editor). Public introspection headers stay plain std types.
 *
 * The one JSON encoding of a property value, shared by the introspection
 * schema (EngineSchemaJson, InspectAsset) and the editor's MCP tools, so a
 * value reads the same wherever a client sees it.
 */
#ifndef MERCURY_INTROSPECTION_VARIANT_JSON_H
#define MERCURY_INTROSPECTION_VARIANT_JSON_H

#include <nlohmann/json.hpp>

#include "serialization/mtext.h"

namespace Mercury::introspection {

/**
 * @brief A Variant value in JSON form
 *
 * nil -> null; bool, int, float and string map to their JSON types;
 * Vector2, Vector3 and Color are plain number arrays ([x, y], [x, y, z],
 * [r, g, b, a]); NodePath -> {"path": "..."}; ExtResource -> {"ext": "id"};
 * Resource -> {"res": "res://..."}; an Array is a JSON array of its items.
 */
nlohmann::json VariantToJson(const mtext::Variant& value);

}  // namespace Mercury::introspection

#endif  // MERCURY_INTROSPECTION_VARIANT_JSON_H
