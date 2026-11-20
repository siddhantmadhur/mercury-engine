// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A tiny editor-system module for the loader tests. It registers nothing and
// calls no editor symbols; the test builds it twice, once with a deliberately
// wrong ABI version.

#include "extension.h"

#ifndef MERCURY_TEST_EXTENSION_ABI
#define MERCURY_TEST_EXTENSION_ABI 1
#endif

namespace {

class TestExtension : public Mercury::EditorUI::EditorExtension {
 public:
  void OnLoad(Mercury::EditorUI::EditorContext& context) override {
    (void)context;
  }
};

}  // namespace

extern "C" const Mercury::EditorUI::EditorExtensionEntry*
mercury_editor_extension_v1() {
  static const Mercury::EditorUI::EditorExtensionEntry entry{
      MERCURY_TEST_EXTENSION_ABI, MERCURY_ENGINE_VERSION,
      []() -> Mercury::EditorUI::EditorExtension* {
        return new TestExtension();
      },
      [](Mercury::EditorUI::EditorExtension* extension) { delete extension; }};
  return &entry;
}
