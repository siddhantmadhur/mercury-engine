#include "event.h"

#include "debugger/debugger.h"
#include "scene/map_scene.h"
#include "scripting/behaviour.h"

namespace Mercury {

const char* ArgTypeName(MethodReflector::ArgType type) {
  switch (type) {
    case MethodReflector::ArgType::kNone: return "";
    case MethodReflector::ArgType::kBool: return "bool";
    case MethodReflector::ArgType::kInt: return "int";
    case MethodReflector::ArgType::kFloat: return "float";
    case MethodReflector::ArgType::kString: return "string";
  }
  return "";
}

bool MethodReflector::Invoke(std::string_view name, const mtext::Variant& argument) const {
  using T = mtext::Variant::Type;
  ArgType wanted = ArgType::kNone;
  switch (argument.GetType()) {
    case T::kNil: wanted = ArgType::kNone; break;
    case T::kBool: wanted = ArgType::kBool; break;
    case T::kInt: wanted = ArgType::kInt; break;
    case T::kFloat: wanted = ArgType::kFloat; break;
    case T::kString: wanted = ArgType::kString; break;
    default: break;
  }
  // Exact argument type first, then any method of that name that can take
  // the value (an int saved for a float parameter, say).
  const MethodInfo* fallback = nullptr;
  for (const auto& method : methods_) {
    if (method.name != name) continue;
    if (method.arg == wanted) {
      method.invoke(argument);
      return true;
    }
    const bool numeric = (method.arg == ArgType::kInt || method.arg == ArgType::kFloat) &&
                         (wanted == ArgType::kInt || wanted == ArgType::kFloat);
    if (numeric || !fallback) fallback = &method;
  }
  if (fallback) {
    fallback->invoke(argument);
    return true;
  }
  return false;
}

void Event::Invoke() const {
  if (scene_) {
    for (const auto& call : calls) {
      GameObject target = scene_->Find(call.target.path);
      if (!target) {
        PDebug::warn("Event: no node at \"{}\" for {}.{}", call.target.path,
                     call.component, call.method);
        continue;
      }
      if (call.component == "GameObject") {
        if (call.method == "SetActive") target.SetActive(call.argument.ToBool().value_or(true));
        continue;
      }
      bool called = false;
      for (Behaviour* behaviour : scene_->GetBehaviours(target.GetEntity())) {
        if (behaviour->GetTypeName() != call.component) continue;
        MethodReflector methods;
        behaviour->ReflectMethods(methods);
        called = methods.Invoke(call.method, call.argument);
        if (called) break;
      }
      if (!called) {
        PDebug::warn("Event: {} has no {} with a method {}", call.target.path,
                     call.component, call.method);
      }
    }
  }
  // Copy: a listener may add or remove listeners.
  const auto listeners = listeners_;
  for (const auto& listener : listeners) listener.fn();
}

Event::ListenerId Event::AddListener(std::function<void()> listener) {
  listeners_.push_back({next_id_, std::move(listener)});
  return next_id_++;
}

void Event::RemoveListener(ListenerId id) {
  std::erase_if(listeners_, [id](const Listener& l) { return l.id == id; });
}

void Event::RemoveAllListeners() { listeners_.clear(); }

mtext::Variant Event::ToVariant(const std::vector<EventCall>& calls) {
  mtext::Array array;
  for (const auto& call : calls) {
    mtext::Array item{mtext::Variant(mtext::NodePath{call.target.path}),
                      mtext::Variant(call.component), mtext::Variant(call.method)};
    if (!call.argument.IsNil()) item.push_back(call.argument);
    array.emplace_back(std::move(item));
  }
  return mtext::Variant(std::move(array));
}

std::optional<std::vector<EventCall>> Event::FromVariant(const mtext::Variant& value) {
  const auto* array = value.Get<mtext::Array>();
  if (!array) return std::nullopt;
  std::vector<EventCall> calls;
  for (const auto& item : *array) {
    const auto* parts = item.Get<mtext::Array>();
    if (!parts || parts->size() < 3) return std::nullopt;
    EventCall call;
    if (auto path = (*parts)[0].Get<mtext::NodePath>()) {
      call.target.path = path->path;
    } else if (auto text = (*parts)[0].Get<std::string>()) {
      call.target.path = *text;
    }
    call.component = (*parts)[1].ToString().value_or("");
    call.method = (*parts)[2].ToString().value_or("");
    if (parts->size() > 3) call.argument = (*parts)[3];
    calls.push_back(std::move(call));
  }
  return calls;
}

}  // namespace Mercury
