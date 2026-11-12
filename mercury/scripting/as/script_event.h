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
