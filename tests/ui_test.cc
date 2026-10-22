#include <gtest/gtest.h>

#include <filesystem>

#include "project/map.h"
#include "project/project.h"
#include "scene/map_scene.h"
#include "ui/binding.h"
#include "ui/controls.h"
#include "ui/data_source.h"
#include "ui/mesh_generator.h"
#include "ui/panel.h"
#include "ui/style_resolver.h"
#include "ui/ui_document.h"
#include "ui/uxml.h"

using namespace Mercury;
using namespace Mercury::UI;

namespace {

// A panel with no theme, so tests only see their own rules.
struct TestPanel {
  UIResources resources;
  Panel panel{&resources};
  TestPanel() {
    resources.Override("test://empty.uss", StyleSheet::Parse(""));
    PanelSettings settings;
    settings.theme_style_sheet = "test://empty.uss";
    panel.SetSettings(settings);
  }
  glm::vec2 size{800, 600};
  void Update() { panel.Update(1.0 / 60.0, size); }
  void Update(glm::vec2 new_size) {
    size = new_size;
    Update();
  }
  VisualElement* root() { return panel.visualTree(); }
};

UIInputFrame PointerAt(glm::vec2 p) {
  UIInputFrame frame;
  frame.pointer = p;
  return frame;
}

void Click(TestPanel& t, glm::vec2 p) {
  UIInputFrame move = PointerAt(p);
  t.panel.DispatchInput(move);
  UIInputFrame frame = PointerAt(p);
  frame.events.push_back({UIInputEvent::Type::kPointerDown, 0});
  frame.events.push_back({UIInputEvent::Type::kPointerUp, 0});
  t.panel.DispatchInput(frame);
  t.Update();
}

}  // namespace

// ------------------------------------------------------------------ USS ---

TEST(Uss, ParsesRulesSelectorsAndValues) {
  std::vector<StyleParseError> errors;
  auto sheet = StyleSheet::Parse(R"(
/* comment */
@import url("base.uss");
:root { --accent: #ff0000; }
.hud, #score > Label:hover {
    width: 50%;
    margin: 4px 8px;
    color: var(--accent);
}
)",
                                 "res://ui/hud.uss", &errors);
  ASSERT_TRUE(errors.empty()) << errors[0].message;
  ASSERT_EQ(sheet->imports.size(), 1u);
  EXPECT_EQ(sheet->imports[0], "res://ui/base.uss");
  ASSERT_EQ(sheet->rules.size(), 2u);
  const StyleRule& rule = sheet->rules[1];
  ASSERT_EQ(rule.selectors.size(), 2u);
  EXPECT_EQ(rule.selectors[0].text, ".hud");
  const ComplexSelector& complex = rule.selectors[1];
  ASSERT_EQ(complex.parts.size(), 2u);
  EXPECT_EQ(complex.parts[0].name, "score");
  EXPECT_EQ(complex.parts[1].type, "Label");
  EXPECT_EQ(complex.parts[1].pseudo, kPseudoHover);
  EXPECT_EQ(complex.combinators[0], ComplexSelector::Combinator::kChild);
  // #id beats classes beats types.
  EXPECT_GT(complex.specificity, rule.selectors[0].specificity);
  EXPECT_EQ(rule.declarations.size(), 3u);
}

TEST(Uss, ReportsErrorsWithLines) {
  std::vector<StyleParseError> errors;
  StyleSheet::Parse(".a {\n  colr: red;\n}\n.b:wat { }", "", &errors);
  ASSERT_EQ(errors.size(), 2u);
  EXPECT_EQ(errors[0].line, 2);
  EXPECT_NE(errors[1].message.find("wat"), std::string::npos);
}

TEST(Uss, ParsesValueTypes) {
  using P = StylePropertyId;
  auto parse = [](P id, const char* text) {
    return ParseStyleValue(id, TokenizeStyleValue(text), "res://ui/a.uss");
  };
  EXPECT_EQ(std::get<Length>(*parse(P::kWidth, "25%")), Length::Percent(25));
  EXPECT_EQ(std::get<Length>(*parse(P::kWidth, "auto")), Length::Auto());
  EXPECT_EQ(std::get<Color>(*parse(P::kColor, "#f00")), Color(1, 0, 0, 1));
  EXPECT_EQ(std::get<Color>(*parse(P::kColor, "rgba(0, 0, 255, 0.5)")), Color(0, 0, 1, 0.5f));
  EXPECT_EQ(std::get<Color>(*parse(P::kColor, "white")), Color(1, 1, 1, 1));
  EXPECT_EQ(std::get<int>(*parse(P::kFlexDirection, "row")),
            static_cast<int>(FlexDirection::kRow));
  EXPECT_EQ(std::get<float>(*parse(P::kBorderLeftWidth, "2px")), 2.0f);
  EXPECT_EQ(std::get<Background>(*parse(P::kBackgroundImage, "url(\"../img/a.png\")")).path,
            "res://img/a.png");
  EXPECT_FLOAT_EQ(std::get<Rotate>(*parse(P::kRotate, "0.5turn")).degrees, 180.0f);
  EXPECT_EQ(std::get<TimeList>(*parse(P::kTransitionDuration, "200ms, 1s")).seconds,
            (std::vector<float>{0.2f, 1.0f}));
  EXPECT_FALSE(parse(P::kWidth, "red"));

  auto expanded = ExpandDeclaration("border-radius", TokenizeStyleValue("1px 2px 3px 4px"));
  ASSERT_EQ(expanded.size(), 4u);
  EXPECT_EQ(expanded[3].id, P::kBorderBottomLeftRadius);
  auto transition = ExpandDeclaration("transition", TokenizeStyleValue("scale 0.3s ease-out"));
  ASSERT_EQ(transition.size(), 4u);
}

TEST(Uss, WriteRoundTrips) {
  auto sheet = StyleSheet::Parse(".a { width: 10px; color: red; }\n#b > .c:hover { flex-grow: 1; }");
  auto again = StyleSheet::Parse(sheet->Write());
  ASSERT_EQ(again->rules.size(), 2u);
  EXPECT_EQ(again->rules[1].SelectorText(), "#b > .c:hover");
  EXPECT_EQ(again->Write(), sheet->Write());
}

// --------------------------------------------------------------- Cascade --

TEST(Cascade, SpecificityOrderInheritanceAndInline) {
  TestPanel t;
  t.root()->AddStyleSheet(StyleSheet::Parse(R"(
:root { --size: 20px; }
Label { color: blue; font-size: var(--size); }
.big { font-size: 30px; }
#title { color: red; }
.parent { color: green; }
.box Label { width: 40px; }
)"));
  auto* parent = t.root()->Emplace<VisualElement>();
  parent->AddToClassList("parent");
  auto* child = parent->Emplace<VisualElement>();
  auto* label = parent->Emplace<Label>("hi");
  label->SetName("title");
  label->AddToClassList("big");
  t.Update();

  EXPECT_NEAR(child->resolvedStyle().color.rgba.g, 128 / 255.0f, 1e-5);  // inherited
  EXPECT_EQ(label->resolvedStyle().color, Color(1, 0, 0, 1));  // #id wins
  EXPECT_EQ(label->resolvedStyle().fontSize, 30.0f);           // class beats type
  EXPECT_TRUE(label->resolvedStyle().width.IsAuto());  // .box doesn't match

  parent->AddToClassList("box");
  label->style().color = Color(1, 1, 0, 1);
  t.Update();
  EXPECT_EQ(label->resolvedStyle().width, Length::Px(40));
  EXPECT_EQ(label->resolvedStyle().color, Color(1, 1, 0, 1));  // inline wins

  label->RemoveFromClassList("big");
  t.Update();
  EXPECT_EQ(label->resolvedStyle().fontSize, 20.0f);  // from the variable
}

TEST(Cascade, PseudoStates) {
  TestPanel t;
  t.root()->AddStyleSheet(StyleSheet::Parse(
      ".b { width: 10px; } .b:hover { width: 20px; } .b:disabled { width: 5px; }"));
  auto* b = t.root()->Emplace<VisualElement>();
  b->AddToClassList("b");
  t.Update();
  EXPECT_EQ(b->resolvedStyle().width, Length::Px(10));
  b->SetPseudoState(kPseudoHover, true);
  t.Update();
  EXPECT_EQ(b->resolvedStyle().width, Length::Px(20));
  b->SetEnabled(false);
  t.Update();
  EXPECT_EQ(b->resolvedStyle().width, Length::Px(5));
}

TEST(Cascade, Transitions) {
  TestPanel t;
  t.root()->AddStyleSheet(StyleSheet::Parse(
      ".b { width: 0px; transition: width 1s linear; } .b.wide { width: 100px; }"));
  auto* b = t.root()->Emplace<VisualElement>();
  b->AddToClassList("b");
  t.Update();
  b->AddToClassList("wide");
  t.Update();  // transition starts at this time
  t.panel.Update(0.5, {800, 600});
  EXPECT_NEAR(b->resolvedStyle().width.value, 50.0f, 2.0f);
  t.panel.Update(0.6, {800, 600});
  EXPECT_EQ(b->resolvedStyle().width, Length::Px(100));
  EXPECT_FALSE(b->HasRunningTransitions());
}

// ---------------------------------------------------------------- Layout --

TEST(Layout, FlexRowGrowPercentAndAbsolute) {
  TestPanel t;
  auto* row = t.root()->Emplace<VisualElement>();
  row->style().flexDirection = FlexDirection::kRow;
  row->style().height = Length::Px(100);
  row->style().SetPadding(Length::Px(10));
  auto* a = row->Emplace<VisualElement>();
  a->style().width = Length::Px(100);
  auto* b = row->Emplace<VisualElement>();
  b->style().flexGrow = 1.0f;
  auto* c = row->Emplace<VisualElement>();
  c->style().width = Length::Percent(25);
  auto* abs = t.root()->Emplace<VisualElement>();
  abs->style().position = Position::kAbsolute;
  abs->style().right = Length::Px(0);
  abs->style().bottom = Length::Px(0);
  abs->style().width = Length::Px(50);
  abs->style().height = Length::Px(20);
  t.Update({800, 600});

  EXPECT_EQ(row->layout(), (Rect{0, 0, 800, 100}));
  EXPECT_EQ(a->layout(), (Rect{10, 10, 100, 80}));  // stretched by align-items
  // 780 inner width: 100 + 25% (195) leaves 485 for b.
  EXPECT_EQ(c->layout().width, 195.0f);
  EXPECT_EQ(b->layout().width, 485.0f);
  EXPECT_EQ(abs->layout(), (Rect{750, 580, 50, 20}));
  EXPECT_EQ(b->worldBound(), (Rect{110, 10, 485, 80}));
}

TEST(Layout, TextIsMeasured) {
  TestPanel t;
  auto* label = t.root()->Emplace<Label>("Hello world");
  label->style().alignSelf = Align::kFlexStart;
  t.Update();
  EXPECT_GT(label->layout().width, 40.0f);
  EXPECT_GT(label->layout().height, 10.0f);
  const float one_line = label->layout().height;

  label->style().width = Length::Px(30);
  t.Update();
  EXPECT_GT(label->layout().height, one_line * 1.5f);  // wrapped
}

TEST(Layout, PanelScaleWithScreenSize) {
  PanelSettings settings;
  settings.scale_mode = PanelScaleMode::kScaleWithScreenSize;
  settings.reference_resolution = {800, 600};
  EXPECT_FLOAT_EQ(ComputePanelScale(settings, {1600, 1200}, 1.0f), 2.0f);
  settings.match = 1.0f;
  EXPECT_FLOAT_EQ(ComputePanelScale(settings, {1600, 600}, 1.0f), 1.0f);
}

// ------------------------------------------------------------------ UXML --

TEST(Uxml, ParseCloneQueryAndWrite) {
  TestPanel t;
  const char* xml = R"(<ui:UXML xmlns:ui="Mercury.UI">
  <ui:VisualElement name="bar" class="top hud" style="height: 40px; flex-direction: row;">
    <ui:Label name="score" text="0" />
    <ui:Button name="pause" text="Pause" />
    <ui:Slider name="volume" low-value="0" high-value="1" value="0.5" />
    <ui:Label name="bound" text="-">
      <Bindings>
        <ui:DataBinding property="text" data-source-path="score" />
      </Bindings>
    </ui:Label>
  </ui:VisualElement>
  <ui:ScrollView name="list"><ui:Label text="in scroll" /></ui:ScrollView>
</ui:UXML>)";
  std::vector<UxmlError> errors;
  auto asset = VisualTreeAsset::Parse(xml, "res://ui/hud.uxml", &errors);
  ASSERT_TRUE(errors.empty()) << errors[0].message;
  asset->CloneTree(t.root(), t.resources);
  t.Update();

  auto* bar = t.root()->Q("bar");
  ASSERT_TRUE(bar);
  EXPECT_TRUE(bar->ClassListContains("hud"));
  EXPECT_EQ(bar->resolvedStyle().flexDirection, FlexDirection::kRow);
  EXPECT_EQ(bar->layout().height, 40.0f);
  ASSERT_TRUE(t.root()->Q<Label>("score"));
  EXPECT_EQ(t.root()->Q<Button>("pause")->text(), "Pause");
  EXPECT_FLOAT_EQ(t.root()->Q<Slider>("volume")->value(), 0.5f);
  EXPECT_EQ(t.root()->Query<Label>().ToList().size(), 3u);
  EXPECT_TRUE(t.root()->Q<Label>("bound")->GetBinding("text"));
  // Children of a ScrollView go into its content container.
  auto* scroll = t.root()->Q<ScrollView>("list");
  EXPECT_EQ(scroll->childCount(), 1);
  EXPECT_EQ(scroll->contentContainer()->childCount(), 1);

  auto again = VisualTreeAsset::Parse(asset->Write());
  EXPECT_EQ(again->Write(), asset->Write());
  EXPECT_EQ(again->root.children[0].children[3].bindings.size(), 1u);
}

TEST(Uxml, TemplatesAndOverrides) {
  TestPanel t;
  t.resources.Override("res://ui/row.uxml",
                       VisualTreeAsset::Parse(R"(<ui:UXML xmlns:ui="Mercury.UI">
    <ui:Label name="title" text="default" />
  </ui:UXML>)",
                                              "res://ui/row.uxml"));
  auto asset = VisualTreeAsset::Parse(R"(<ui:UXML xmlns:ui="Mercury.UI">
    <ui:Template name="Row" src="row.uxml" />
    <ui:Instance template="Row" name="first">
      <ui:AttributeOverrides element-name="title" text="Row 1" />
    </ui:Instance>
    <ui:Instance template="Row" name="second" />
  </ui:UXML>)",
                                      "res://ui/list.uxml");
  asset->CloneTree(t.root(), t.resources);
  EXPECT_EQ(t.root()->Q("first")->Q<Label>("title")->text(), "Row 1");
  EXPECT_EQ(t.root()->Q("second")->Q<Label>("title")->text(), "default");
  EXPECT_TRUE(dynamic_cast<TemplateContainer*>(t.root()->Q("first")));
}

// ---------------------------------------------------------------- Events --

TEST(Events, TrickleDownTargetBubbleUpAndStop) {
  TestPanel t;
  auto* outer = t.root()->Emplace<VisualElement>();
  outer->style().width = Length::Px(100);
  outer->style().height = Length::Px(100);
  auto* inner = outer->Emplace<VisualElement>();
  inner->style().width = Length::Px(50);
  inner->style().height = Length::Px(50);
  t.Update();

  std::vector<std::string> order;
  outer->RegisterCallback<PointerDownEvent>(
      [&](PointerDownEvent&) { order.push_back("outer-trickle"); }, TrickleDown::kTrickleDown);
  outer->RegisterCallback<PointerDownEvent>([&](PointerDownEvent&) { order.push_back("outer"); });
  inner->RegisterCallback<PointerDownEvent>([&](PointerDownEvent&) { order.push_back("inner"); });

  UIInputFrame frame = PointerAt({10, 10});
  frame.events.push_back({UIInputEvent::Type::kPointerDown, 0});
  t.panel.DispatchInput(frame);
  EXPECT_EQ(order, (std::vector<std::string>{"outer-trickle", "inner", "outer"}));

  order.clear();
  inner->RegisterCallback<PointerDownEvent>([](PointerDownEvent& e) { e.StopPropagation(); });
  t.panel.DispatchInput(frame);
  EXPECT_EQ(order, (std::vector<std::string>{"outer-trickle", "inner"}));

  // Picking ignores elements with picking-mode Ignore.
  inner->SetPickingMode(PickingMode::kIgnore);
  EXPECT_EQ(t.panel.Pick({10, 10}), outer);
  EXPECT_EQ(t.panel.Pick({200, 200}), nullptr);
}

TEST(Events, ButtonClickHoverAndDisabled) {
  TestPanel t;
  auto* button = t.root()->Emplace<Button>("Go");
  button->style().width = Length::Px(80);
  button->style().height = Length::Px(30);
  t.Update();
  int clicks = 0;
  button->clicked += [&] { clicks++; };

  Click(t, {10, 10});
  EXPECT_EQ(clicks, 1);
  EXPECT_TRUE(button->pseudoStates() & kPseudoHover);
  EXPECT_TRUE(t.panel.IsPointerOverUI());
  EXPECT_EQ(t.panel.focusedElement(), button);

  // Released outside: no click.
  UIInputFrame down = PointerAt({10, 10});
  down.events.push_back({UIInputEvent::Type::kPointerDown, 0});
  t.panel.DispatchInput(down);
  UIInputFrame up = PointerAt({300, 300});
  up.events.push_back({UIInputEvent::Type::kPointerUp, 0});
  t.panel.DispatchInput(up);
  EXPECT_EQ(clicks, 1);
  EXPECT_FALSE(button->pseudoStates() & kPseudoHover);

  button->SetEnabled(false);
  Click(t, {10, 10});
  EXPECT_EQ(clicks, 1);

  // Enter submits the focused button.
  button->SetEnabled(true);
  button->Focus();
  UIInputFrame enter;
  enter.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kEnter});
  t.panel.DispatchInput(enter);
  EXPECT_EQ(clicks, 2);
}

TEST(Events, ToggleSliderAndTabFocus) {
  TestPanel t;
  auto* toggle = t.root()->Emplace<Toggle>();
  toggle->style().height = Length::Px(20);
  auto* slider = t.root()->Emplace<Slider>(0.0f, 100.0f);
  slider->style().height = Length::Px(20);
  slider->visualInput()->style().flexGrow = 1.0f;
  slider->style().flexDirection = FlexDirection::kRow;
  t.Update({200, 100});

  bool changed = false;
  toggle->RegisterValueChangedCallback<bool>([&](ChangeEvent<bool>& e) {
    changed = e.newValue && !e.previousValue;
  });
  Click(t, {5, 5});
  EXPECT_TRUE(toggle->value());
  EXPECT_TRUE(changed);
  EXPECT_TRUE(toggle->pseudoStates() & kPseudoChecked);

  Click(t, {150, 30});  // 3/4 along the slider
  EXPECT_NEAR(slider->value(), 75.0f, 2.0f);

  t.panel.focusController().SwitchFocus(nullptr);
  UIInputFrame tab;
  tab.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kTab});
  t.panel.DispatchInput(tab);
  EXPECT_EQ(t.panel.focusedElement(), toggle);
  t.panel.DispatchInput(tab);
  EXPECT_EQ(t.panel.focusedElement(), slider);
}

TEST(Events, TextFieldEditing) {
  TestPanel t;
  t.panel.clipboard.set("pasted");
  auto* field = t.root()->Emplace<TextField>();
  t.Update();
  field->Focus();
  std::string last;
  field->RegisterValueChangedCallback<std::string>(
      [&](ChangeEvent<std::string>& e) { last = e.newValue; });

  UIInputFrame typing;
  for (char32_t c : std::u32string(U"héllo")) {
    typing.events.push_back({UIInputEvent::Type::kChar, 0, KeyCode::kNone, c});
  }
  typing.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kBackspace});
  typing.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kHome});
  typing.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kV, 0, {}, kModControl});
  t.panel.DispatchInput(typing);
  EXPECT_EQ(field->value(), "pastedhéll");
  EXPECT_EQ(last, "pastedhéll");

  UIInputFrame select_all;
  select_all.events.push_back({UIInputEvent::Type::kKeyDown, 0, KeyCode::kA, 0, {}, kModControl});
  select_all.events.push_back({UIInputEvent::Type::kChar, 0, KeyCode::kNone, U'x'});
  t.panel.DispatchInput(select_all);
  EXPECT_EQ(field->value(), "x");

  field->SetMaxLength(3);
  UIInputFrame more;
  for (char32_t c : std::u32string(U"abcdef")) {
    more.events.push_back({UIInputEvent::Type::kChar, 0, KeyCode::kNone, c});
  }
  t.panel.DispatchInput(more);
  EXPECT_EQ(field->value(), "xab");
}

// --------------------------------------------------------------- Binding --

namespace {
struct Stats {
  int score = 7;
  float volume = 0.25f;
  std::string name = "Ann";
  void Reflect(Reflector& r) {
    r.Field("score", score);
    r.Field("volume", volume);
    r.Property("greeting", [this] { return mtext::Variant("Hi " + name); });
  }
};
}  // namespace

TEST(Binding, ToTargetTwoWayAndReflectedSources) {
  TestPanel t;
  Stats stats;
  t.root()->SetDataSource(ReflectedDataSource::Of(&stats));
  auto* label = t.root()->Emplace<Label>();
  label->SetBinding("text", DataBinding{"score"});
  auto* greeting = t.root()->Emplace<Label>();
  greeting->SetBinding("text", DataBinding{"greeting"});
  auto* slider = t.root()->Emplace<Slider>(0.0f, 1.0f);
  slider->SetBinding("value", DataBinding{"volume", BindingMode::kTwoWay});
  t.Update();
  EXPECT_EQ(label->text(), "7");
  EXPECT_EQ(greeting->text(), "Hi Ann");
  EXPECT_FLOAT_EQ(slider->value(), 0.25f);

  stats.score = 12;
  t.Update();
  EXPECT_EQ(label->text(), "12");

  slider->SetValue(0.75f);
  t.Update();
  EXPECT_FLOAT_EQ(stats.volume, 0.75f);

  auto bag = std::make_shared<UIDataSource>();
  bag->Set("score", 99);
  label->SetDataSource(bag);
  t.Update();
  EXPECT_EQ(label->text(), "99");
}

// ------------------------------------------------------------------ Mesh --

TEST(Mesh, DrawsBoxesTextAndClips) {
  TestPanel t;
  auto* box = t.root()->Emplace<VisualElement>();
  box->style().width = Length::Px(100);
  box->style().height = Length::Px(50);
  box->style().backgroundColor = Color(1, 0, 0, 1);
  box->style().SetBorderWidth(2.0f);
  box->style().SetBorderColor(Color(0, 0, 0, 1));
  box->style().SetBorderRadius(Length::Px(8));
  box->style().overflow = Overflow::kHidden;
  box->Emplace<Label>("text");
  t.Update();
  const UIDrawData& data = t.panel.drawData();
  ASSERT_FALSE(data.empty());
  EXPECT_GT(data.vertices.size(), 50u);
  EXPECT_EQ(data.target_size, glm::ivec2(800, 600));
  // Background + border share a batch; the text uses the font atlas.
  bool clipped_text = false;
  for (const auto& command : data.commands) {
    if (command.texture_id != 0 && command.clip == glm::ivec4(2, 2, 96, 46)) {
      clipped_text = true;
    }
    EXPECT_LE(command.index_offset + command.index_count, data.indices.size());
  }
  EXPECT_TRUE(clipped_text);
  for (uint32_t index : data.indices) ASSERT_LT(index, data.vertices.size());

  // Nothing changed: the mesh is not rebuilt, so it stays identical.
  const size_t before = data.vertices.size();
  t.Update();
  EXPECT_EQ(t.panel.drawData().vertices.size(), before);
}

TEST(Mesh, DefaultThemeLoads) {
  UIResources resources;
  auto theme = resources.DefaultTheme();
  ASSERT_TRUE(theme);
  EXPECT_GT(theme->rules.size(), 10u);
  Panel panel(&resources);
  auto* button = panel.visualTree()->Emplace<Button>("Hi");
  panel.Update(0.016, {400, 300});
  // The default theme gives buttons a background and a border.
  EXPECT_GT(button->resolvedStyle().backgroundColor.rgba.a, 0.0f);
  EXPECT_GT(button->resolvedStyle().borderTopWidth, 0.0f);
}

// ------------------------------------------------------------ UIDocument --

TEST(UIDocument, ShowsTheMonkeyBallHudInPlayMode) {
  const auto root = std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/monkey_ball";
  std::string error;
  auto project = Project::Load(root / "monkey_ball.mproj", &error);
  ASSERT_TRUE(project) << error;
  Map map;
  ASSERT_TRUE(map.Load(root / "maps/main.mmap", &error)) << error;

  MapScene scene(project, MapScene::Mode::kPlay);
  scene.SetWidth(1280);
  scene.SetHeight(720);
  scene.Instantiate(map);
  auto* document = scene.Find("GameManager").GetComponent<UIDocument>();
  ASSERT_TRUE(document);
  ASSERT_TRUE(document->rootVisualElement());
  EXPECT_TRUE(document->rootVisualElement()->Q<Label>("score"));
  EXPECT_TRUE(document->rootVisualElement()->Q<Button>("resume"));

  scene.OnFrame(16.0);
  ASSERT_TRUE(scene.GetUIDrawData());
  EXPECT_GT(scene.GetUIDrawData()->commands.size(), 0u);
  // ScaleWithScreenSize at the reference resolution is 1:1.
  EXPECT_FLOAT_EQ(document->runtimePanel()->scale(), 1.0f);

  // Destroying the node removes its document from the panel.
  scene.Destroy(scene.Find("GameManager"));
  scene.OnFrame(16.0);
  EXPECT_EQ(document = scene.Find("GameManager").GetComponent<UIDocument>(), nullptr);
  EXPECT_EQ(scene.GetUI().panels().front()->visualTree()->childCount(), 0);
}

TEST(UIDocument, EditModeDoesNotBuildUI) {
  const auto root = std::filesystem::path(MERCURY_SOURCE_DIR) / "examples/monkey_ball";
  std::string error;
  auto project = Project::Load(root / "monkey_ball.mproj", &error);
  ASSERT_TRUE(project) << error;
  Map map;
  ASSERT_TRUE(map.Load(root / "maps/main.mmap", &error)) << error;
  MapScene scene(project, MapScene::Mode::kEdit);
  scene.Instantiate(map);
  auto* document = scene.Find("GameManager").GetComponent<UIDocument>();
  ASSERT_TRUE(document);
  EXPECT_EQ(document->rootVisualElement(), nullptr);
  EXPECT_EQ(scene.GetUIDrawData(), nullptr);
}

// ------------------------------------------------------- More controls --

TEST(Controls, ListViewSelectsAndScrollViewScrolls) {
  TestPanel t;
  std::vector<std::string> items;
  for (int i = 0; i < 50; i++) items.push_back("Item " + std::to_string(i));
  auto* list = t.root()->Emplace<ListView>();
  list->style().height = Length::Px(200);
  list->makeItem = [] { return std::make_unique<Label>(); };
  list->bindItem = [&](VisualElement* e, int i) { static_cast<Label*>(e)->SetText(items[i]); };
  list->SetFixedItemHeight(20);
  list->SetItemCount(static_cast<int>(items.size()));
  std::vector<int> selected;
  list->selectionChanged += [&](const std::vector<int>& s) { selected = s; };
  t.Update();
  t.Update();
  EXPECT_EQ(list->scrollView()->contentContainer()->childCount(), 50);
  EXPECT_FALSE(list->scrollView()->verticalScroller()->resolvedStyle().display ==
               DisplayStyle::kNone);

  Click(t, {50, 50});  // the third item
  EXPECT_EQ(list->selectedIndex(), 2);
  EXPECT_EQ(selected, std::vector<int>{2});

  UIInputFrame wheel = PointerAt({50, 50});
  UIInputEvent e;
  e.type = UIInputEvent::Type::kWheel;
  e.wheel = {0, 3};
  wheel.events.push_back(e);
  t.panel.DispatchInput(wheel);
  t.Update();
  EXPECT_GT(list->scrollView()->scrollOffset().y, 10.0f);
  // Items scrolled up under the pointer.
  Click(t, {50, 50});
  EXPECT_GT(list->selectedIndex(), 2);
}

TEST(Controls, Painter2DAndScheduler) {
  TestPanel t;
  auto* canvas = t.root()->Emplace<VisualElement>();
  canvas->style().width = Length::Px(100);
  canvas->style().height = Length::Px(100);
  int draws = 0;
  canvas->generateVisualContent = [&](MeshGenerationContext& ctx) {
    draws++;
    auto& p = ctx.painter2D();
    p.strokeColor = Color(1, 1, 1, 1);
    p.lineWidth = 4;
    p.BeginPath();
    p.Arc({50, 50}, 40, 0, 270);
    p.Stroke();
    p.fillColor = Color(1, 0, 0, 1);
    p.BeginPath();
    p.MoveTo({10, 10});
    p.LineTo({90, 10});
    p.LineTo({50, 90});
    p.ClosePath();
    p.Fill();
  };
  t.Update();
  EXPECT_EQ(draws, 1);
  EXPECT_GT(t.panel.drawData().vertices.size(), 100u);

  int ticks = 0;
  canvas->Schedule([&] { ticks++; }).Every(0.0).Until([&] { return ticks >= 3; });
  for (int i = 0; i < 5; i++) t.Update();
  EXPECT_EQ(ticks, 3);
}
