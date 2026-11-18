// Registers C++ Behaviours for engine tests. Game scripts can use the same
// MERCURY_SCRIPT API; this helper keeps the test name parameterizable.
#ifndef MERCURY_TESTS_TEST_SCRIPTS_H
#define MERCURY_TESTS_TEST_SCRIPTS_H

#include <string>
#include <type_traits>

#include "scripting/behaviour.h"

namespace Mercury {

// Lets maps use the Behaviour subclass T by name, like an AngelScript
// [script] class or MERCURY_SCRIPT would.
template <typename T>
bool RegisterTestBehaviour(const char* name) {
  return RegisterScript<T>(name);
}

}  // namespace Mercury

#define MERCURY_TEST_BEHAVIOUR(Type)                                    \
  [[maybe_unused]] static const bool mercury_test_behaviour_##Type##_ = \
      ::Mercury::RegisterScript<Type>(#Type)

#endif  // MERCURY_TESTS_TEST_SCRIPTS_H
