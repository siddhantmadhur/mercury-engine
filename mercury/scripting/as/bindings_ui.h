// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * bindings_ui.h
 */
#ifndef MERCURY_SCRIPTING_AS_BINDINGS_UI_H
#define MERCURY_SCRIPTING_AS_BINDINGS_UI_H

#include <cstdint>

namespace Mercury {

// A UI element on the script side. The engine keeps a table of live elements
// and invalidates every handle when a document is destroyed.
struct ScriptUiHandle {
  int64_t value = -1;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_BINDINGS_UI_H
