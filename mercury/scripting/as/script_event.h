// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * script_event.h
 *
 * The value a script field of type Event holds. The engine owns the real
 * Mercury::Event in ScriptBehaviour::events_ and gives the field an id for it
 * after the script object is constructed. Scripts copy this value freely and
 * may keep it after the behaviour is gone, so it is a handle, not a pointer:
 * ScriptRuntime::FindEvent turns it back into the Event while the owner is
 * alive and returns null afterwards. Ids are never reused.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H
#define MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H

#include <cstdint>

namespace Mercury {

class Event;

struct ScriptEvent {
  uint32_t id = 0;  // 0: not bound to an Event
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H
