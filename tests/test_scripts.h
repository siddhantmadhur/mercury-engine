// Registers C++ Behaviours for engine tests. Game scripts are AngelScript;
// these stand in for them when a test is about MapScene itself rather than the
// script runtime.
#ifndef MERCURY_TESTS_TEST_SCRIPTS_H
#define MERCURY_TESTS_TEST_SCRIPTS_H

#include <string>
#include <type_traits>

#include "scripting/behaviour.h"

namespace Mercury {

// Lets maps use the Behaviour subclass T by name, like an AngelScript [script]
// class would.
template <typename T>
bool RegisterTestBehaviour(const char* name) {
  static_assert(std::is_base_of_v<Behaviour, T>,
                "test behaviours must derive from Behaviour");
  ComponentInfo info;
  info.name = name;
  info.category = "Scripts";
  info.is_script = true;
  {
    T instance{};
    auto reflector = Reflector::Describe();
    instance.Reflect(reflector);
    info.fields = reflector.GetFields();
    MethodReflector methods;
    instance.ReflectMethods(methods);
    for (const auto& method : methods.GetMethods()) {
      info.methods.push_back({method.name, ArgTypeName(method.arg)});
    }
  }
  info.attach = [name = std::string(name)](
                    MapScene& scene, kEntityId entity,
                    const mtext::PropertyList& properties) {
    auto behaviour = std::make_unique<T>();
    auto reflector = Reflector::Read(properties);
    behaviour->Reflect(reflector);
    LogReflectionWarnings(reflector,
                          scene.GetGameObject(entity).GetPath() + "." + name);
    scene.AddBehaviour(entity, std::move(behaviour), name);
  };
  TypeRegistry::Instance().RegisterComponent(std::move(info));
  return true;
}

}  // namespace Mercury

#define MERCURY_TEST_BEHAVIOUR(Type)                                    \
  [[maybe_unused]] static const bool mercury_test_behaviour_##Type##_ = \
      ::Mercury::RegisterTestBehaviour<Type>(#Type)

#endif  // MERCURY_TESTS_TEST_SCRIPTS_H
