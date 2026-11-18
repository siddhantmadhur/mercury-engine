// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MERCURY_EXAMPLES_CPP_HELLO_SPINNER_H
#define MERCURY_EXAMPLES_CPP_HELLO_SPINNER_H

#include "scripting/behaviour.h"

// A C++ gameplay component, registered with MERCURY_SCRIPT in spinner.cc.
class Spinner : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("speed", speed).Tooltip("Degrees per second");
  }

  void OnFrame(double dt) override {
    auto& transform = GetTransform();
    transform.SetRotate(transform.GetRotate() +
                        glm::vec3(0.0f, speed * static_cast<float>(dt), 0.0f));
  }

  float speed = 45.0f;
};

#endif  // MERCURY_EXAMPLES_CPP_HELLO_SPINNER_H
