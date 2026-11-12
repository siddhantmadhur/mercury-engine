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
