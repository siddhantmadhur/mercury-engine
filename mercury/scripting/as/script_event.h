// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * script_event.h
 *
 * The value a script field of type Event holds. The engine owns the real
 * Mercury::Event in ScriptBehaviour::events_, and binds the pointer after the
 * script object is constructed.
 */
#ifndef MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H
#define MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H

namespace Mercury {

class Event;

struct ScriptEvent {
  Event* impl = nullptr;
};

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_AS_SCRIPT_EVENT_H
