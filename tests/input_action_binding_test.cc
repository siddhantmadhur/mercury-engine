// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Regression test for #541: InputAction methods registered with
// asCALL_CDECL_OBJFIRST must receive the script object by reference, not by
// value. A by-value ReadVector2 reads the address of the script field as the
// action index and throws "The input action no longer exists" in onTick.

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "input/input_manager.h"
#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "scripting/as/script_builder.h"
#include "scripting/as/script_runtime.h"
#include "scripting/type_registry.h"
#include "test_paths.h"

using namespace Mercury;
namespace fs = std::filesystem;

namespace {

const char* kProbeScript = R"(
// Appends one character per successful read so C++ can verify the call.
[script]
class InputActionProbe : Behaviour
{
    private InputAction move;
    private InputAction jump;

    void onStart()
    {
        move = Input::RequireAction("Player/Move");
        jump = Input::RequireAction("Player/Jump");
        GameObject object = GetGameObject();
        object.tag = object.tag + "s";
    }

    void onTick(double dt)
    {
        Vector2 value = move.ReadVector2();
        float amount = jump.ReadFloat();
        GameObject object = GetGameObject();
        if (value.y > 0.5f) object.tag = object.tag + "v";
        if (amount > 0.5f) object.tag = object.tag + "f";
    }
}
)";

class InputActionBindingTest : public ::testing::Test {
 protected:
  void TearDown() override { ScriptRuntime::Instance().Shutdown(); }
};

TEST_F(InputActionBindingTest, ScriptCanReadActionsInOnTick) {
  const fs::path source = fs::path(MERCURY_SOURCE_DIR) / "examples/roll_a_ball";
  const fs::path work = test::CacheDir() / "input_action_binding";
  std::error_code ec;
  fs::remove_all(work, ec);
  fs::create_directories(work / "scripts", ec);
  for (const std::string& part :
       {"scripts", "input", "maps", "roll_a_ball.mproj"}) {
    fs::copy(source / part, work / part, fs::copy_options::recursive, ec);
    ASSERT_FALSE(ec) << part << ": " << ec.message();
  }
  std::ofstream(work / "scripts/input_action_probe.as") << kProbeScript;

  std::string log;
  ASSERT_TRUE(ScriptBuilder::Build(work, {}, &log)) << log;
  RegisterBuiltinTypes();
  ASSERT_TRUE(ScriptRuntime::Instance().Initialize());
  ASSERT_TRUE(ScriptRuntime::Instance().LoadScripts(work));

  std::string error;
  auto project = Project::Load(work / "roll_a_ball.mproj", &error);
  ASSERT_TRUE(project) << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  Map map;
  ASSERT_TRUE(map.LoadFromString(R"([mmap]
format = 2

[[component]]
node = "A"
type = "InputActionProbe"

[[node]]
name = "Root"

[[node]]
name = "A"
parent = "."
)",
                                 &error))
      << error;
  scene.Instantiate(map);
  scene.OnStart();

  InputManager input;
  scene.SetInputManager(&input);
  input.BeginFrameFreshPresses();
  input.InjectKeyEvent(GLFW_KEY_W, GLFW_PRESS);
  input.InjectKeyEvent(GLFW_KEY_SPACE, GLFW_PRESS);
  scene.OnFrame(1000.0 / 60.0);  // InputActionAsset::Update runs per frame.
  scene.OnTickBegin();
  scene.OnTick(1000.0 / 60.0);
  scene.SimulatePhysics(1);

  // "s" means onStart resolved both handles; "v" and "f" mean onTick read both
  // of them without an exception. The old by-value ReadVector2 threw before
  // appending "v", so this captures the regression in-process.
  EXPECT_EQ(scene.Find("A").GetTag(), "svf");
}

}  // namespace
