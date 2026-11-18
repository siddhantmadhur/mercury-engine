// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURY_RUST_BINDINGS_H
#define MERCURY_RUST_BINDINGS_H

#include <cstdint>

#include "rust/cxx.h"

namespace Mercury::rust {

void LogDebug(::rust::Str message);
void LogInfo(::rust::Str message);
void LogWarn(::rust::Str message);
void LogError(::rust::Str message);

bool OsExists(::rust::Str path);
bool OsIsPacked(::rust::Str path);
bool OsHasMounts();
bool OsReadBytes(::rust::Str path, ::rust::Vec<uint8_t>& out);
::rust::String OsCacheDir();
::rust::String OsExecutableDir();

::rust::Str EngineVersion();

}  // namespace Mercury::rust

#endif  // MERCURY_RUST_BINDINGS_H
