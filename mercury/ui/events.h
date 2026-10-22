/*
 * events.h
 *
 * UI events, modeled on Unity UI Toolkit. An event travels from the panel
 * root down to its target (TrickleDown), reaches the target (AtTarget) and,
 * if it bubbles, goes back up (BubbleUp):
 *
 *   button->RegisterCallback<ClickEvent>([](ClickEvent& e) { ... });
 *   root->RegisterCallback<PointerDownEvent>(fn, TrickleDown::kTrickleDown);
 *   field->RegisterValueChangedCallback<std::string>(
 *       [](ChangeEvent<std::string>& e) { e.newValue; });
 */
#ifndef MERCURY_UI_EVENTS_H
#define MERCURY_UI_EVENTS_H

#include <cstdint>
#include <glm/glm.hpp>
#include <string>

#include "ui/input.h"
#include "ui/style.h"

namespace Mercury::UI {

class VisualElement;

using EventTypeId = uint32_t;

EventTypeId NextEventTypeId();

enum class PropagationPhase : uint8_t { kNone, kTrickleDown, kAtTarget, kBubbleUp };
enum class TrickleDown : uint8_t { kNoTrickleDown, kTrickleDown };

class EventBase {
 public:
  virtual ~EventBase() = default;
  virtual EventTypeId eventTypeId() const = 0;

  bool bubbles() const { return bubbles_; }
  bool tricklesDown() const { return trickles_down_; }
  VisualElement* target() const { return target_; }
  VisualElement* currentTarget() const { return current_target_; }
  PropagationPhase propagationPhase() const { return phase_; }
  double timestamp() const { return timestamp_; }

  void StopPropagation() { propagation_stopped_ = true; }
  void StopImmediatePropagation() {
    propagation_stopped_ = true;
    immediate_stopped_ = true;
  }
  bool isPropagationStopped() const { return propagation_stopped_; }
  bool isImmediatePropagationStopped() const { return immediate_stopped_; }
  // Stops the engine's own handling (focus moves, text input, ...).
  void PreventDefault() { default_prevented_ = true; }
  bool isDefaultPrevented() const { return default_prevented_; }

  // Set by the dispatcher.
  void SetTarget(VisualElement* target) { target_ = target; }
  void SetCurrentTarget(VisualElement* target) { current_target_ = target; }
  void SetPhase(PropagationPhase phase) { phase_ = phase; }
  void SetTimestamp(double t) { timestamp_ = t; }

 protected:
  EventBase(bool bubbles, bool trickles_down)
      : bubbles_(bubbles), trickles_down_(trickles_down) {}

 private:
  bool bubbles_;
  bool trickles_down_;
  bool propagation_stopped_ = false;
  bool immediate_stopped_ = false;
  bool default_prevented_ = false;
  VisualElement* target_ = nullptr;
  VisualElement* current_target_ = nullptr;
  PropagationPhase phase_ = PropagationPhase::kNone;
  double timestamp_ = 0.0;
};

// Gives each event class a unique, stable type id.
template <typename T>
class Event : public EventBase {
 public:
  static EventTypeId TypeId() {
    static const EventTypeId id = NextEventTypeId();
    return id;
  }
  EventTypeId eventTypeId() const override { return TypeId(); }

 protected:
  using EventBase::EventBase;
};

// ------------------------------------------------------------ Pointer -----

template <typename T>
class PointerEventBase : public Event<T> {
 public:
  int pointerId = 0;     // 0 is the mouse
  int button = -1;       // 0 left, 1 right, 2 middle; -1 for moves
  int pressedButtons = 0;  // bit per button
  glm::vec2 position{0.0f};       // panel coordinates
  glm::vec2 localPosition{0.0f};  // relative to the current target
  glm::vec2 deltaPosition{0.0f};
  int clickCount = 0;
  KeyModifiers modifiers = kModNone;

  bool shiftKey() const { return modifiers & kModShift; }
  bool ctrlKey() const { return modifiers & kModControl; }
  bool altKey() const { return modifiers & kModAlt; }

 protected:
  PointerEventBase(bool bubbles = true, bool trickles = true)
      : Event<T>(bubbles, trickles) {}
};

class PointerDownEvent : public PointerEventBase<PointerDownEvent> {};
class PointerUpEvent : public PointerEventBase<PointerUpEvent> {};
class PointerMoveEvent : public PointerEventBase<PointerMoveEvent> {};
class PointerCancelEvent : public PointerEventBase<PointerCancelEvent> {};
// Enter/Leave go to each element the pointer enters or leaves, without
// bubbling; Over/Out go to the topmost element and bubble.
class PointerEnterEvent : public PointerEventBase<PointerEnterEvent> {
 public:
  PointerEnterEvent() : PointerEventBase(false, true) {}
};
class PointerLeaveEvent : public PointerEventBase<PointerLeaveEvent> {
 public:
  PointerLeaveEvent() : PointerEventBase(false, true) {}
};
class PointerOverEvent : public PointerEventBase<PointerOverEvent> {};
class PointerOutEvent : public PointerEventBase<PointerOutEvent> {};
// A press and release on the same element.
class ClickEvent : public PointerEventBase<ClickEvent> {};

class PointerCaptureEvent : public Event<PointerCaptureEvent> {
 public:
  PointerCaptureEvent() : Event(false, true) {}
};
class PointerCaptureOutEvent : public Event<PointerCaptureOutEvent> {
 public:
  PointerCaptureOutEvent() : Event(false, true) {}
};

class WheelEvent : public PointerEventBase<WheelEvent> {
 public:
  glm::vec2 delta{0.0f};  // scroll amount; +y scrolls down
};

// ----------------------------------------------------------- Keyboard -----

template <typename T>
class KeyboardEventBase : public Event<T> {
 public:
  KeyCode keyCode = KeyCode::kNone;
  // A typed character, or 0. Text input arrives as KeyDownEvents with only
  // the character set, as in Unity.
  char32_t character = 0;
  KeyModifiers modifiers = kModNone;

  bool shiftKey() const { return modifiers & kModShift; }
  bool ctrlKey() const { return modifiers & kModControl; }
  bool altKey() const { return modifiers & kModAlt; }
  // Ctrl, or Cmd on macOS.
  bool actionKey() const { return modifiers & (kModControl | kModSuper); }

 protected:
  KeyboardEventBase() : Event<T>(true, true) {}
};

class KeyDownEvent : public KeyboardEventBase<KeyDownEvent> {};
class KeyUpEvent : public KeyboardEventBase<KeyUpEvent> {};

// --------------------------------------------------------- Navigation -----

enum class NavigationDirection : uint8_t { kNone, kLeft, kUp, kRight, kDown, kNext, kPrevious };

class NavigationMoveEvent : public Event<NavigationMoveEvent> {
 public:
  NavigationMoveEvent() : Event(true, true) {}
  NavigationDirection direction = NavigationDirection::kNone;
  glm::vec2 move{0.0f};
  KeyModifiers modifiers = kModNone;
};
class NavigationSubmitEvent : public Event<NavigationSubmitEvent> {
 public:
  NavigationSubmitEvent() : Event(true, true) {}
};
class NavigationCancelEvent : public Event<NavigationCancelEvent> {
 public:
  NavigationCancelEvent() : Event(true, true) {}
};

// -------------------------------------------------------------- Focus -----

template <typename T>
class FocusEventBase : public Event<T> {
 public:
  VisualElement* relatedTarget = nullptr;

 protected:
  FocusEventBase(bool bubbles) : Event<T>(bubbles, true) {}
};
// FocusOut/FocusIn bubble; Blur/Focus do not.
class FocusOutEvent : public FocusEventBase<FocusOutEvent> {
 public:
  FocusOutEvent() : FocusEventBase(true) {}
};
class BlurEvent : public FocusEventBase<BlurEvent> {
 public:
  BlurEvent() : FocusEventBase(false) {}
};
class FocusInEvent : public FocusEventBase<FocusInEvent> {
 public:
  FocusInEvent() : FocusEventBase(true) {}
};
class FocusEvent : public FocusEventBase<FocusEvent> {
 public:
  FocusEvent() : FocusEventBase(false) {}
};

// ---------------------------------------------------------- Change etc ----

template <typename T>
class ChangeEvent : public Event<ChangeEvent<T>> {
 public:
  ChangeEvent() : Event<ChangeEvent<T>>(true, true) {}
  ChangeEvent(T previous, T value)
      : Event<ChangeEvent<T>>(true, true),
        previousValue(std::move(previous)),
        newValue(std::move(value)) {}
  T previousValue{};
  T newValue{};
};

struct Rect {
  float x = 0, y = 0, width = 0, height = 0;
  glm::vec2 position() const { return {x, y}; }
  glm::vec2 size() const { return {width, height}; }
  float xMax() const { return x + width; }
  float yMax() const { return y + height; }
  glm::vec2 center() const { return {x + width / 2, y + height / 2}; }
  bool Contains(glm::vec2 p) const {
    return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
  }
  bool operator==(const Rect&) const = default;
};

class GeometryChangedEvent : public Event<GeometryChangedEvent> {
 public:
  GeometryChangedEvent() : Event(false, false) {}
  Rect oldRect, newRect;
};

class AttachToPanelEvent : public Event<AttachToPanelEvent> {
 public:
  AttachToPanelEvent() : Event(false, false) {}
};
class DetachFromPanelEvent : public Event<DetachFromPanelEvent> {
 public:
  DetachFromPanelEvent() : Event(false, false) {}
};

// Sent after an element's style is resolved, when it has custom (--name)
// properties; read them with element->customStyle().
class CustomStyleResolvedEvent : public Event<CustomStyleResolvedEvent> {
 public:
  CustomStyleResolvedEvent() : Event(false, false) {}
};

// Sent when a transition starts or ends on a property.
class TransitionEndEvent : public Event<TransitionEndEvent> {
 public:
  TransitionEndEvent() : Event(true, true) {}
  StylePropertyId property = StylePropertyId::kCount;
  double elapsedTime = 0.0;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_EVENTS_H
