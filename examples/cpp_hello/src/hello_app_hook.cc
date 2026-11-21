// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A process-wide hook: it reads a custom game option before the window
// exists. Unknown --options are only accepted by the player when an AppHook
// is registered. Run the example with:
//
//   ./cpp_hello cpp_hello.mproj --greeting "hello from the command line"

#include <debugger/debugger.h>
#include <scripting/app_hook.h>

#include <string>

namespace {

class GreetingHook : public Mercury::AppHook {
 public:
  bool OnPreInit(std::span<const std::string_view> args) override {
    for (size_t i = 0; i < args.size(); i++) {
      if (args[i] == "--greeting" && i + 1 < args.size()) {
        greeting_ = std::string(args[i + 1]);
      }
    }
    return true;
  }

  bool OnInit() override {
    PDebug::info("Hello: {}",
                 greeting_.empty()
                     ? "pass --greeting <text> to customize this line"
                     : greeting_);
    return true;
  }

 private:
  std::string greeting_;
};

}  // namespace

MERCURY_APP_HOOK(GreetingHook);
