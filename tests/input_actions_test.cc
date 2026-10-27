#include <gtest/gtest.h>

#include "GLFW/glfw3.h"
#include "input/input_actions.h"
#include "input/input_actions_codegen.h"

using namespace Mercury;

namespace {

InputActionsDesc SampleActions() {
  InputActionsDesc desc;
  desc.maps = {"Player", "UI", "Empty"};
  desc.class_name = "GameInput";
  desc.header = "res://scripts/game_input.h";
  InputActionDesc jump;
  jump.map = "Player";
  jump.name = "Jump";
  jump.bindings = {"<Keyboard>/space", "<Gamepad>/buttonSouth"};
  InputActionDesc move;
  move.map = "Player";
  move.name = "Move";
  move.type = InputAction::Type::kValue;
  move.vector2 = true;
  move.bindings = {"<Gamepad>/leftStick"};
  move.composites.push_back({{"<Keyboard>/w", "<Keyboard>/upArrow"},
                             {"<Keyboard>/s"},
                             {"<Keyboard>/a"},
                             {"<Keyboard>/d"}});
  InputActionDesc submit;
  submit.map = "UI";
  submit.name = "Submit";
  submit.type = InputAction::Type::kPassThrough;
  desc.actions = {jump, move, submit};
  return desc;
}

}  // namespace

TEST(InputActions, SerializeThenParseGivesTheSameActions) {
  const InputActionsDesc desc = SampleActions();
  InputActionsDesc parsed;
  std::string error;
  ASSERT_TRUE(ParseInputActions(SerializeInputActions(desc), &parsed, &error)) << error;
  EXPECT_EQ(parsed, desc);
}

TEST(InputActions, ParsesFilesWithoutMapSections) {
  constexpr const char* kSource = R"([minput format=1]

[action name="Player/Jump" type="button"]
bindings = ["<Keyboard>/space"]

[action name="Look" type="passthrough" control="vector2"]
bindings = "<Mouse>/delta"
)";
  InputActionsDesc desc;
  ASSERT_TRUE(ParseInputActions(kSource, &desc));
  EXPECT_EQ(desc.maps, (std::vector<std::string>{"Player", ""}));
  ASSERT_EQ(desc.actions.size(), 2u);
  EXPECT_EQ(desc.actions[1].FullName(), "Look");
  EXPECT_EQ(desc.actions[1].type, InputAction::Type::kPassThrough);
  EXPECT_TRUE(desc.actions[1].vector2);
  EXPECT_EQ(desc.actions[1].bindings, std::vector<std::string>{"<Mouse>/delta"});
}

TEST(InputActions, AssetLoadsFromSerializedActions) {
  InputActionAsset asset;
  ASSERT_TRUE(asset.LoadFromString(SerializeInputActions(SampleActions())));
  ASSERT_NE(asset.Find("Player/Move"), nullptr);
  EXPECT_EQ(asset.Find("Player/Move")->GetType(), InputAction::Type::kValue);
  EXPECT_NE(asset.Find("Jump"), nullptr);
  EXPECT_EQ(asset.GetMap("Player").size(), 2u);
}

TEST(InputActions, ControlNamesAndKeysAreInverse) {
  for (int key : {GLFW_KEY_A, GLFW_KEY_Z, GLFW_KEY_0, GLFW_KEY_F5, GLFW_KEY_KP_3,
                  GLFW_KEY_SPACE, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_UP, GLFW_KEY_PAGE_DOWN}) {
    const std::string control = ControlFromKey(key);
    ASSERT_FALSE(control.empty()) << key;
    EXPECT_EQ(KeyFromControl(control), key) << control;
  }
  EXPECT_EQ(ControlFromKey(GLFW_KEY_LEFT_SHIFT), "leftShift");
  EXPECT_EQ(KeyFromControl("LEFTSHIFT"), GLFW_KEY_LEFT_SHIFT);
  EXPECT_EQ(ControlFromGamepadButton(GLFW_GAMEPAD_BUTTON_A), "buttonSouth");
}

TEST(InputActions, KnownControlPathsAreReadable) {
  bool has_space = false;
  for (std::string_view path : KnownControlPaths()) {
    ASSERT_TRUE(path.starts_with("<")) << path;
    if (path.starts_with("<Keyboard>/") && path != "<Keyboard>/shift" &&
        path != "<Keyboard>/ctrl" && path != "<Keyboard>/anyKey") {
      EXPECT_NE(KeyFromControl(path.substr(11)), GLFW_KEY_UNKNOWN) << path;
    }
    has_space |= path == "<Keyboard>/space";
  }
  EXPECT_TRUE(has_space);
}

TEST(InputActionsCodegen, MakesIdentifiers) {
  EXPECT_EQ(InputActionIdentifier("move left"), "MoveLeft");
  EXPECT_EQ(InputActionIdentifier("fire-2"), "Fire2");
  EXPECT_EQ(InputActionIdentifier("2d look"), "_2dLook");
  EXPECT_EQ(InputActionIdentifier(""), "Default");
}

TEST(InputActionsCodegen, GeneratesOneStructPerMap) {
  const auto header = GenerateInputActionsHeader(SampleActions(), "GameInput",
                                                 "res://input/actions.minput");
  EXPECT_TRUE(header.errors.empty());
  const std::string& s = header.source;
  EXPECT_NE(s.find("struct GameInput {"), std::string::npos);
  EXPECT_NE(s.find("struct PlayerActions {"), std::string::npos);
  EXPECT_NE(s.find("Mercury::InputAction* Jump;"), std::string::npos);
  EXPECT_NE(s.find("struct EmptyActions {"), std::string::npos);
  EXPECT_NE(s.find("kPlayerMove = \"Player/Move\""), std::string::npos);
  EXPECT_NE(s.find("Player{asset.Find(\"Player/Jump\"), asset.Find(\"Player/Move\")}"),
            std::string::npos);
  EXPECT_NE(s.find("UI{asset.Find(\"UI/Submit\")}"), std::string::npos);
  EXPECT_NE(s.find("Empty{}"), std::string::npos);
}

TEST(InputActionsCodegen, ReportsNameCollisions) {
  InputActionsDesc desc;
  desc.maps = {"Player", "player!"};
  InputActionDesc a;
  a.map = "Player";
  a.name = "Fire 1";
  InputActionDesc b = a;
  b.name = "fire-1";
  desc.actions = {a, b};
  const auto header = GenerateInputActionsHeader(desc, "GameInput", "x");
  EXPECT_EQ(header.errors.size(), 2u);
}
