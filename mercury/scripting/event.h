/*
 * event.h
 *
 * Event, the C++ version of Unity's UnityEvent: a list of calls saved with
 * the map (set up in the editor) plus listeners added from code.
 *
 *   class Zone : public Mercury::Behaviour {
 *     void Reflect(Mercury::Reflector& r) override { r.Field("entered", entered); }
 *     void OnTriggerEnter(Mercury::GameObject) override { entered.Invoke(); }
 *     Mercury::Event entered;
 *   };
 *
 * A saved call names a node, a script on it and one of the methods that
 * script lists in ReflectMethods, with an optional argument:
 *
 *   entered = [[NodePath("Player"), "Health", "TakeDamage", 15.0]]
 */
#ifndef MERCURY_SCRIPTING_EVENT_H
#define MERCURY_SCRIPTING_EVENT_H

#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "scripting/reflection.h"
#include "serialization/mtext.h"

namespace Mercury {

class MapScene;

struct EventCall {
  NodeRef target;
  std::string component;  // script (or engine component) type name
  std::string method;
  mtext::Variant argument;  // nil: the method takes no argument
};

class Event {
 public:
  using ListenerId = int;

  // Runs every saved call, then every listener added from code.
  void Invoke() const;

  ListenerId AddListener(std::function<void()> listener);
  void RemoveListener(ListenerId id);
  // Removes the listeners added from code; saved calls stay.
  void RemoveAllListeners();
  size_t GetListenerCount() const { return listeners_.size(); }

  // Saved calls, read from the map.
  std::vector<EventCall> calls;

  // Converts between the saved array form and calls.
  static mtext::Variant ToVariant(const std::vector<EventCall>& calls);
  static std::optional<std::vector<EventCall>> FromVariant(
      const mtext::Variant& value);

  // Set by MapScene when the owning script is added, so saved calls can find
  // their targets.
  void Bind(MapScene* scene) { scene_ = scene; }

 private:
  struct Listener {
    ListenerId id;
    std::function<void()> fn;
  };
  MapScene* scene_ = nullptr;
  std::vector<Listener> listeners_;
  ListenerId next_id_ = 1;
};

/**
 * @brief Collects the methods a script lets Events call
 *
 *   void ReflectMethods(Mercury::MethodReflector& m) override {
 *     m.Method("Heal", [this] { Heal(); });
 *     m.Method("TakeDamage", [this](float amount) { TakeDamage(amount); });
 *   }
 *
 * The same name may be listed with different argument types, like C#
 * overloads; a call picks the one matching its argument.
 */
class MethodReflector {
 public:
  enum class ArgType { kNone, kBool, kInt, kFloat, kString };

  struct MethodInfo {
    std::string name;
    ArgType arg = ArgType::kNone;
    std::function<void(const mtext::Variant&)> invoke;
  };

  // fn takes nothing, or one bool, int, float or std::string.
  template <typename F>
  MethodReflector& Method(const char* name, F&& fn) {
    using Signature = decltype(&std::decay_t<F>::operator());
    AddMethod<Signature>(name, std::function(std::forward<F>(fn)));
    return *this;
  }

  const std::vector<MethodInfo>& GetMethods() const { return methods_; }

  // Calls the method `name` that best matches `argument`. False if none.
  bool Invoke(std::string_view name, const mtext::Variant& argument) const;

 private:
  template <typename Signature, typename Fn>
  void AddMethod(const char* name, Fn fn) {
    using Args = typename Arguments<Signature>::type;
    if constexpr (std::tuple_size_v<Args> == 0) {
      methods_.push_back({name, ArgType::kNone, [fn](const mtext::Variant&) { fn(); }});
    } else {
      using A = std::decay_t<std::tuple_element_t<0, Args>>;
      if constexpr (std::is_same_v<A, bool>) {
        methods_.push_back({name, ArgType::kBool, [fn](const mtext::Variant& v) {
                              fn(v.ToBool().value_or(false));
                            }});
      } else if constexpr (std::is_integral_v<A>) {
        methods_.push_back({name, ArgType::kInt, [fn](const mtext::Variant& v) {
                              fn(static_cast<A>(v.ToInt().value_or(0)));
                            }});
      } else if constexpr (std::is_floating_point_v<A>) {
        methods_.push_back({name, ArgType::kFloat, [fn](const mtext::Variant& v) {
                              fn(static_cast<A>(v.ToFloat().value_or(0.0)));
                            }});
      } else {
        static_assert(std::is_convertible_v<std::string, A>,
                      "Event methods take nothing, a bool, int, float or string");
        methods_.push_back({name, ArgType::kString, [fn](const mtext::Variant& v) {
                              fn(v.ToString().value_or(""));
                            }});
      }
    }
  }

  // The parameter types of a lambda's call operator.
  template <typename T>
  struct Arguments;
  template <typename C, typename R, typename... A>
  struct Arguments<R (C::*)(A...) const> {
    using type = std::tuple<A...>;
  };
  template <typename C, typename R, typename... A>
  struct Arguments<R (C::*)(A...)> {
    using type = std::tuple<A...>;
  };

  std::vector<MethodInfo> methods_;
};

const char* ArgTypeName(MethodReflector::ArgType type);

}  // namespace Mercury

#endif  // MERCURY_SCRIPTING_EVENT_H
