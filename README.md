# Mercury

Mercury is a game-engine built with C++ for performant games

<img width="854" height="508" alt="mercury_engine_example" src="https://github.com/user-attachments/assets/dc234997-66ed-462a-9bb8-d86c7bafa2c5">

## Table of Contents

- [Goal](#goal)
- [Support](#support)
- [Building](#building)
- [Issues](#issues)
- [Contributing](#contributing)
- [Design](#design)
- [LICENSE](#license)

## Goal

A completely open-source program that is designed to be develop performant video-games. The primary goal
is to develop simulator video-games like Railroad Tycoon, SimCity, Cities Skylines etc. This means that it
is developed specifically for top-down games with lots of systems being simulated, and features like ray-tracing,
or advanced graphics might never be supported.

## Support

A goal of this project was to be cross-platform. Since everything is being implemented
myself it is difficult to and thus I'm only aiming for desktop platforms with a limited number
of graphics backends.

The order of the platform is the order of importance for compatibility.
For example, Windows has the most support and hence is the first on the list.
If a platform is not on the list then that means supporting is not a goal of this project.

| Platform      | Renderer | Support |
|---------------|----------|---------|
| Windows 10/11 | Vulkan   | Yes     |
| Linux         | Vulkan   | Yes     |
| SteamOS       | Vulkan   | No [^1] |
| MacOS         | MoltenVK | None    |

Rendering requires a GPU and driver with Vulkan 1.3 (dynamic rendering). Shaders are
written in HLSL and compiled to SPIR-V with DXC at build time.

[^1]: The goal is to have SteamOS specific features like support for those touchpads on the
SteamDeck or the controllers. Along with support for controller layouts, battery modes and more.

## Building

### Linux (X11 and Wayland)

```sh
./scripts/setup_project_linux.sh        # apt or pacman; installs Vulkan, GLFW deps and DXC
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Debian/Ubuntu don't package DXC, so the setup script downloads the official release into
`bin/dxc/`, which CMake finds automatically. If you use the
[LunarG Vulkan SDK](https://vulkan.lunarg.com) instead, set `VULKAN_SDK` and its `dxc` is used.

Debug builds enable `VK_LAYER_KHRONOS_validation` when it is installed.

### Windows

Install the [LunarG Vulkan SDK](https://vulkan.lunarg.com) (it provides the loader, validation
layers and DXC, and sets `VULKAN_SDK`), then configure with CMake or open the folder in Visual
Studio (`CMakeSettings.json`).

## Issues

Any bugs related to the game engine can be reported at [issues.reocelot.com](https://issues.reocelot.com)

## Contributing

Mercury is open-source but not open-contribution. Features part of this engine
are specifically for games **I** want to make and not to be generalized.
In fact there are much better engines already out there that do most things
better so I'd recommend using that if you cannot find a feature here.

That being said, there are many instances where you might want a change urgently (bugs, vulnerabilities)
and perhaps already fixed it, in which case email [maintainer@mercuryengine.com](mailto:maintainer@mercuryengine.com) with
more information or with
a [git patch](https://git-scm.com/book/en/v2/Distributed-Git-Contributing-to-a-Project#_project_over_email).

Official repositories for the code:

- https://oss.reocelot.com/mercury-engine
- https://github.com/siddhantmadhur/mercury-engine [mirror]

## Design

Mercury is designed as a C++ library that can be used to produce games. This involves
tasks such as windowing, shaders, input, memory and more. There are two parts of
its design. First is its implementation which is how you can get started with adding
Mercury to your project, or building using it. And the other is it's internal design
which is how I've decided to go about designing the program which you hopefully never
see.

You can find detailed documentation at: [docs.mercuryengine.com](https://docs.mercuryengine.com)

Some important design features to better understand the engine:

### Application

Mercury::Application is responsible for handling cross-platform application level
features such as creating a window, taking in user-input like mouse, keyboard or
controller.

The goal was to minimize platform-specific code to as little of the codebase as
possible. This is why the next branch of the engine are Scenes and Primitives.

### Scenes

Mercury::GameScene are a state of the game that should be extended to create the world.
They only implement higher level functions like managing object properties, or the physics system
and don't implement platform specific features like drawing or windowing.

### Projects and the editor

Games are made as projects, the way Unity and Godot work, instead of by hand-writing a
`GameScene`. `examples/pong` (2D) and `examples/roll_a_ball` (3D, after Unity's tutorial) are
complete examples:

```
pong.mproj              project settings (name, main map, window size)
maps/main.mmap          the map: nodes and their components
assets/*.masset         materials and physics materials
scripts/*.h, *.cc       game logic as C++ components
CMakeLists.txt          mercury_add_game(pong PROJECT ${CMAKE_CURRENT_SOURCE_DIR})
```

Open it with the editor:

```sh
./build/mercury_editor examples/pong/pong.mproj    # or no argument for the project launcher
```

The editor has a Hierarchy, an Inspector, a Scene view, a Project browser and a Console. It has no
code editor; write scripts in any IDE and set the command it opens them with under Project >
External editor command. **GameObject** (or **+ Create** in the Hierarchy, or right-click a node
for a child) adds empty nodes, 3D objects (cube, sphere, capsule, cylinder, plane, quad, each with
a matching collider), 2D sprites, directional lights and cameras. New projects from the launcher
start from a 2D or a 3D template.

The Scene view opens in 2D or 3D to suit the map; the **2D** button switches. Click to select,
drag to move (along the ground in 3D), and use the **Move / Rotate / Scale** handles (W / E / R);
hold Ctrl to snap. In 2D, right or middle drag pans and the wheel zooms. In 3D, right drag looks
around (add W/A/S/D/Q/E to fly, Shift to go faster), Alt + left drag orbits, middle drag pans and
the wheel zooms. F frames the selection. **Play**
saves the map, builds the game with CMake, and runs it as its own process, with its output in the
Console. **Build** only compiles, which refreshes the script list the Inspector uses. New projects
made from the launcher build against this engine checkout.

Each game is its own executable, such as `./build/examples/pong/pong`. With no arguments it opens
the project it was built from; it also takes `[project.mproj] [--map res://maps/x.mmap]`.

#### File formats

`.mproj`, `.mmap` and `.masset` share one text format, modeled on Godot's `.tscn` so that people
and LLMs can read, diff and edit it by hand. A file is a list of `[sections]`, each followed by
`key = value` lines:

```ini
[mmap format=1]

[ext_resource type="Material" path="res://assets/white.masset" id="1"]

[node name="Pong"]

[node name="Ball" parent="."]

[component type="Transform" node="Ball"]
scale = Vector3(2, 2, 1)

[component type="SpriteRenderer" node="Ball"]
material = ExtResource("1")

[component type="Ball" node="Ball"]
speed = 60.0
```

- `parent=` and `node=` are paths from the root node: `.` is the root, `Ball` is a child of the
  root, and `Walls/Left` is a grandchild. Parents come before their children.
- Values are `true`, `3`, `1.5`, `"text"`, `Vector2(x, y)`, `Vector3(x, y, z)`,
  `Color(r, g, b, a)`, `NodePath("Walls/Left")`, `ExtResource("id")`, `Resource("res://...")`
  or `[arrays]`. Comments start with `;`, but they are not kept when the editor saves.
- `res://` paths are relative to the folder that holds the `.mproj`.
- Fields left at their default value are omitted.

Built-in components:

| Component | What it does |
|---|---|
| `Transform` | `position`, `rotation` (Euler degrees, applied X, then Y, then Z) and `scale` |
| `SpriteRenderer` | An unlit quad on the XY plane, sized by the scale (2D) |
| `MeshRenderer` | A lit `mesh`: `cube`, `sphere`, `capsule`, `cylinder`, `plane`, `quad`, or `model` with a `.obj`/`.gltf`/`.glb`/`.fbx` file |
| `Camera` | Looks along its -Z. `fit_height` makes a 2D camera; also `fov`, clip distances, `background` |
| `DirectionalLight` | Sunlight along its -Z, with `color`, `intensity` and `ambient` |
| `Rigidbody` | Static, kinematic or dynamic motion, `mass`, locked axes, damping, gravity factor |
| `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `CylinderCollider`, `PlaneCollider` | Collision shapes, scaled by the Transform; `is_trigger` reports contacts without pushing |
| `UIDocument` | Shows a `.uxml` document on screen (see UI Toolkit below) |

Shapes have unit size at scale 1 (a 1 x 1 x 1 cube, a sphere 1 across, a capsule 1 across and 2
tall), like Unity's. Asset types are `Material`, `PhysicsMaterial` and `PanelSettings`. Gravity is set in the
`.mproj` with `[physics] gravity = Vector3(0, -9.81, 0)`. A node's position is relative to its
parent; maps are flattened to world positions when loaded, and rotation and scale are not
inherited.

#### Scripts

A script is a C++ class deriving from `Mercury::Behaviour`, registered with `MERCURY_SCRIPT`:

```cpp
class Ball : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override { r.Field("speed", speed); }
  void OnStart() override;                          // all objects exist
  void OnTick(double dt) override;                  // 60 times a second, before physics
  void OnFrame(double dt) override;                 // every frame; ImGui dev windows allowed
  void OnCollisionEnter(Mercury::GameObject other) override;
  float speed = 60.0f;
};
MERCURY_SCRIPT(Ball);
```

Scripts can push bodies with `Physics().AddForce(GetEntity(), force)` (also `AddImpulse`,
`AddTorque`, `SetAngularVelocity`), turn things with `GetTransform().SetRotate(...)`, and remove
objects with `Destroy(object)`.

Fields listed in `Reflect` appear in the Inspector and are saved in maps. Supported field types are
bool, int, float, std::string, glm::vec2/vec3, glm::vec4 (a color), `NodeRef`, `AssetRef` and
enums (`r.Enum(...)`). The editor never loads game code. After each build it runs
`game --dump-schema .mercury/scripts.mschema` to learn the scripts' fields. `.mercury/` holds
generated files and should not be committed.

#### UI Toolkit

In-game UI (HUDs, menus, inventories, scores) is built with Mercury's UI Toolkit, a copy of
Unity's: layouts in **UXML**, styles in **USS**, a tree of `VisualElement`s laid out with flexbox
(Yoga, the engine Unity uses) and drawn by the engine's own renderer, with SDF text. Dear ImGui is
for the editor and for developer/debug windows only; don't use it for what players see.

```xml
<!-- ui/hud.uxml -->
<ui:UXML xmlns:ui="Mercury.UI">
    <Style src="hud.uss" />
    <ui:Label name="score" class="score">
        <Bindings><ui:DataBinding property="text" data-source-path="score" /></Bindings>
    </ui:Label>
    <ui:Button name="pause" text="Pause" />
</ui:UXML>
```

```css
/* ui/hud.uss */
.score { font-size: 32px; color: #FFE433; -unity-font-style: bold; }
.unity-button:hover { scale: 1.05 1.05; transition: scale 0.15s ease-out; }
```

Add a `UIDocument` component to a node (`source_asset` is the `.uxml`, `panel_settings` an
optional `PanelSettings` asset that sets the scaling and theme), then in a script:

```cpp
void ReflectData(Mercury::Reflector& r) override {  // bindable, not saved
  r.Field("score", score_);
  r.Property("time", [this] { return Mercury::mtext::Variant(FormatTime()); });
}
void OnStart() override {
  auto* root = GetDocument()->rootVisualElement();
  root->SetDataSource(AsDataSource());              // bindings read ReflectData/Reflect
  root->Q<Mercury::UI::Button>("pause")->clicked += [this] { Pause(); };
  root->Q("banner")->EnableInClassList("hidden", false);
}
```

It has the controls Unity has (Label, Button, Toggle, RadioButton, Slider, SliderInt,
ProgressBar, TextField, IntegerField, FloatField, ScrollView, ListView, Foldout, GroupBox, Image),
`Q<T>()`/`Query<T>()`, events that trickle down and bubble up (`RegisterCallback<ClickEvent>`,
`ChangeEvent<T>`, pointer, keyboard, navigation and focus events), USS selectors with
`:hover`/`:active`/`:focus`/`:checked`/`:disabled`, variables, transitions, 9-slicing, rich text
and `generateVisualContent` with a `Painter2D` for custom drawing. The default look is
`mercury/ui/resources/default_theme.uss`, after Unity's runtime theme.

**Window > UI Builder** (or double-clicking a `.uxml`) edits documents visually like Unity's UI
Builder: style sheets and selectors, the element hierarchy, a library to drag controls from, a live
canvas drawn by the game's renderer (with a Preview mode to click through it), and an Inspector
for attributes, classes, bindings and inline or selector styles. **+ Create** in the Project panel
makes new UI documents, style sheets and panel settings.

#### Testing

`ctest --test-dir build` runs the unit tests for the formats, maps, reflection, primitive
meshes, rotations and the UI Toolkit (USS, UXML, cascade, layout, events, bindings, drawing).
`mercury_editor_smoke <project> <script>` replays scripted input into the editor. Set
`MERCURY_CAPTURE=frame.png` (plus `MERCURY_CAPTURE_FRAME=n` and `MERCURY_CAPTURE_QUIT=1`) on any
Mercury program to save a screenshot of a frame.

### Primitives

Primitive is a basic struct that is the minimum requirement to render an object to the screen.
Scenes can modify or create primitives and implement them however they want. For example if they
want a single texture atlas and contain a bunch of objects in their vertices so less draw calls
are called, that's upto the scene to implement.

When the Application finally receives the Primitives, it creates buffers out of it and draws
them to the screen. Again the way primitives are understood by Application is quite simple
and thus optimizing how primitives are created or pooled (for batch rendering) should be
easy to implement on all platforms then.

## License

[GNU GPLv3](/LICENSE)
