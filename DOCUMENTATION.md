# Mercury Engine Documentation

How Mercury works, from the application loop to the renderer. For what Mercury is, how to build it and its license, see the [README](README.md). Planned work is in [GOAL.md](GOAL.md).

## Contents

- [Overview](#overview)
- [Projects and the editor](#projects-and-the-editor)
- [File formats](#file-formats)
- [Components](#components)
- [Rendering](#rendering)
- [Scripts](#scripts)
- [UI Toolkit](#ui-toolkit)
- [Shipping a game](#shipping-a-game)
- [Importing a Unity project](#importing-a-unity-project)
- [Testing](#testing)

## Overview

Mercury is a C++ library for producing games, covering windowing, input, rendering, physics, audio, UI and memory. Its design has three layers:

- **Application** (`Mercury::Application`) handles the platform-level work: creating the window and taking mouse, keyboard and controller input. Platform-specific code is kept to as little of the codebase as possible.
- **Scenes** (`Mercury::GameScene`) are a state of the game. They manage objects, their properties and physics, and know nothing about drawing or windowing.
- **Primitives** are the minimum a scene must hand over to get an object on screen. The Application turns them into buffers and draws them. How primitives are batched or pooled (for example one texture atlas to cut draw calls) is up to the scene.

Source layout under `mercury/`: `application`, `scene`, `components`, `pipelines` (Vulkan rendering), `shaders` (HLSL), `physics_engine`, `animation`, `audio`, `input`, `ui`, `scripting`, `serialization`, `project`, `pack` and `os`.

## Projects and the editor

Games are projects, like in Unity and Godot, rather than hand-written `GameScene`s. `examples/pong` (2D) and `examples/roll_a_ball` (3D, after Unity's tutorial) are complete examples. `examples/model_viewer` loads scenes from the McGuire Computer Graphics Archive (Sponza, the Stanford Bunny, Cornell boxes, ...) from a menu, with fly-through benchmark cameras that report per-lap frame rates.

```
pong.mproj              project settings (name, main map, window size)
maps/main.mmap          the map: nodes and their components
assets/*.masset         materials and physics materials
scripts/*.h, *.cc       game logic as C++ components
CMakeLists.txt          mercury_add_game(pong PROJECT ${CMAKE_CURRENT_SOURCE_DIR})
```

Open a project with the editor:

```sh
./build/mercury_editor examples/pong/pong.mproj    # or no argument for the project launcher
```

The editor has a Hierarchy, Inspector, Scene view, Project browser and Console. It has no code editor: write scripts in any IDE and set the command that opens them under Project > External editor command.

- **Creating nodes:** GameObject menu, **+ Create** in the Hierarchy, or right-click a node for a child. You can add empty nodes, 3D objects (cube, sphere, capsule, cylinder, plane, quad, each with a matching collider), 2D sprites, directional lights and cameras. New projects from the launcher start from a 2D or 3D template.
- **Scene view:** opens in 2D or 3D to suit the map (the **2D** button switches). Click to select, drag to move (along the ground in 3D), **Move / Rotate / Scale** handles on W / E / R, Ctrl to snap. F frames the selection.
  - 2D: right or middle drag pans, wheel zooms.
  - 3D: right drag looks around (W/A/S/D/Q/E to fly, Shift for speed), Alt + left drag orbits, middle drag pans, wheel zooms.
- **Play** saves the map, builds the game with CMake and runs it as its own process, with output in the Console. **Build** only compiles, which refreshes the script list the Inspector uses. Projects made from the launcher build against this engine checkout.

Each game is its own executable, e.g. `./build/examples/pong/pong`. With no arguments it opens the project it was built from; it also takes `[project.mproj] [--map res://maps/x.mmap]`.

## File formats

`.mproj`, `.mmap` and `.masset` share one text format modeled on Godot's `.tscn`, so people and LLMs can read, diff and edit it by hand. A file is a list of `[sections]`, each followed by `key = value` lines:

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

- `parent=` and `node=` are paths from the root node: `.` is the root, `Ball` a child of the root, `Walls/Left` a grandchild. Parents come before their children.
- Values are `true`, `3`, `1.5`, `"text"`, `Vector2(x, y)`, `Vector3(x, y, z)`, `Color(r, g, b, a)`, `NodePath("Walls/Left")`, `ExtResource("id")`, `Resource("res://...")` or `[arrays]`. Comments start with `;` but are not kept when the editor saves.
- `res://` paths are relative to the folder holding the `.mproj`.
- Fields left at their default value are omitted.

Other files:

- `.mprefab`: a node tree in the map format. A map node with `instance="res://gun.mprefab"` copies it; component sections for the instance (or its children) override single values, and `removed=true` drops a component. Scripts copy with `Instantiate(...)`.
- `.manim`: an animator controller (`[parameter]`, `[layer]`, `[state]` with a clip or a 1D/2D blend tree, `[transition]` with conditions and exit time).
- `.minput`: input actions (Unity's Input System): `[action name="Player/Jump" type="button"]` with `bindings` such as `"<Keyboard>/space"` and `"<Gamepad>/buttonSouth"`, and 2D vector `composites`. `[map name="Player"]` sections list the action maps, and the `[minput]` header's `class` and `header` name the generated C++ header. The `.mproj` names it in `[input] actions`. Edit it in **Window > Input Actions**.
- `.mimport` next to a model: `convention = "unity"` imports it the Unity way (180 degrees about Y, file units); `[import] model_convention` in the `.mproj` sets the default. `scale` resizes it and `max_texture_size` (default 8192) scales larger textures down.

Models bring their own materials: base color and texture, opacity, emission (`Ke`, `map_Ke`), normal maps (or `bump` height maps, converted to normal maps on import) and, for OBJ, glossiness from `Ns`/`Ks`. Texture names are matched case-insensitively.

`.mproj` sections: `[physics]` (`gravity = Vector3(0, -9.81, 0)`, `tick_rate = 50`), `[layers] names`, `[tags] names` (set on a node with `layer=` and `tag=`), `[render]` (SSAO and outline settings), `[input]`, `[import]` and `[build]`.

## Components

A node's position is relative to its parent; maps are flattened to world positions when loaded, and rotation and scale are not inherited. Shapes have unit size at scale 1 (1x1x1 cube, sphere 1 across, capsule 1 across and 2 tall), like Unity's. Asset types are `Material`, `PhysicsMaterial`, `PanelSettings`, `RenderTexture`, `PostProcessProfile` and any data assets scripts declare.

| Component | What it does |
|---|---|
| `Transform` | `position`, `rotation` (Euler degrees, applied X, then Y, then Z) and `scale` |
| `SpriteRenderer` | An unlit quad on the XY plane, sized by the scale (2D) |
| `MeshRenderer` | A lit `mesh`: `cube`, `sphere`, `capsule`, `cylinder`, `plane`, `quad`, or `model` with a `.obj`/`.gltf`/`.glb`/`.fbx` file (node tree, materials, skin and clips; `node` draws one part, `materials` overrides them) |
| `Camera` | Looks along its -Z. `fit_height` makes a 2D camera; also `fov`, clip distances, `background`, `clear`, `culling_mask`, `depth`, `overlay` (drawn over `base_camera`), `target_texture`, `post_processing` |
| `DirectionalLight` | Sunlight along its -Z: `color`, `intensity`, `ambient`, `ground_ambient`, `cast_shadows` (see [Lighting](#lighting)) |
| `PointLight` | Light in every direction: `color`, `intensity`, `range` (Unity's falloff; up to 16 per map). Soft cube-map shadows that widen with `radius`; `shadows = "hard"` or `"none"` changes that |
| `AreaLight` | A glowing rectangle of `size` in the node's XY plane, shining along -Z. `color` * `intensity` is its radiance. Exact diffuse lighting, soft shadows; up to 4 per map, 8 shadowed point and area lights in all |
| `LightProbeVolume` | Settings for global illumination (see [Global illumination](#global-illumination)): `size`, `resolution`, `fit_to_scene`, `bounces`, `intensity` (0 turns it off) |
| `Rigidbody` | Static, kinematic or dynamic motion, `mass`, locked axes, damping, gravity |
| `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `CylinderCollider`, `PlaneCollider`, `MeshCollider` | Collision shapes, scaled by the Transform, with a `center`; a node may have several. `is_trigger` reports overlaps without pushing |
| `CharacterController` | Unity's: `Move(displacement)`, `IsGrounded()`, slopes and steps, resizable at run time |
| `Animator` | Plays a `.manim` controller (parameters, blend trees, transitions, layers) on the node's model |
| `AudioSource`, `AudioListener` | Sound playback (miniaudio) with 3D attenuation; `Play`, `Stop`, `PlayOneShot` |
| `TextMesh` | SDF text in the world (TextMeshPro 3D) |
| `PostProcessVolume` | Picks a `PostProcessProfile` asset (see [Post-processing](#post-processing)) |
| `UIDocument` | Shows a `.uxml` document on screen (see [UI Toolkit](#ui-toolkit)) |

## Rendering

Rendering uses Vulkan 1.3 with dynamic rendering. Shaders are written in HLSL (`mercury/shaders/`) and compiled to SPIR-V by DXC at build time (CMake finds `dxc` in `bin/dxc/`, `VULKAN_SDK` or the system). Shader files are named by stage: `*.vs.hlsl` vertex, `*.ps.hlsl` pixel, `*.cs.hlsl` compute, with shared code in `*.hlsli`.

### Frame outline

1. **Culling:** `cull.cs.hlsl` runs on the GPU, one thread per draw slot, testing each against a view and appending indexed indirect draws to four buckets (single/double-sided, normal/mirrored). Views then draw with `vkCmdDrawIndexedIndirectCount`.
2. **Shadow passes:** the sun's maps, and cube maps for point and area lights, are drawn with `shadow.vs`/`model_shadow.vs` and `shadow_cube.ps`. Glass is drawn into transmittance maps (`shadow_transmit.ps`).
3. **Probe capture** (when lights or still geometry change): see [Global illumination](#global-illumination).
4. **Main pass:** `model.vs`/`model.ps` draw lit models into a linear HDR target, plus a view-space normal target used by SSAO. `sky.ps` draws the background and `skybox` a cubemap/panorama.
5. **Glass:** after the opaque pass the scene is copied (`scene_copy.ps`) so transmissive surfaces can sample what is behind them.
6. **Post-processing:** fullscreen passes (`fullscreen.vs` + `post_common.hlsli`) for SSAO, bloom, FXAA, outline and the final composite.
7. **UI:** `ui.vs`/`ui.ps` draw the UI Toolkit on top.

### Lighting

Surfaces use Cook-Torrance GGX shading with a metallic/roughness material model. Light sources:

- **Directional light:** PCF shadows from a detailed shadow map covering `shadow_distance` around the camera, plus a scene-wide map of the still geometry (redrawn only when it or the sun changes) that shadows everything farther away.
- **Point lights:** Unity's falloff, soft cube-map shadows whose softness follows `radius`.
- **Area lights:** rectangles with exact diffuse lighting and shadows whose penumbra grows with distance from the caster.
- **Shadow cost control:** point/area shadow maps are redrawn only when the light or something near it moves.
- **Glass and transmissive materials** refract the scene behind them (blurred by roughness, tinted, under a Fresnel reflection), and tint light passing through them via the transmittance maps. Without a scene copy (overlays, RenderTextures) they just alpha-blend.

### Global illumination

Every map has GI. Without a `LightProbeVolume` component the renderer fits a grid of light probes around the still geometry.

- Each probe captures the lit scene (sun shadowed over the whole grid, ambient colors as the sky) as six cube faces, which `probe_sh.ps` projects to L2 spherical harmonics. `probe_octa.ps` and `probe_visibility.ps` build octahedral irradiance and distance maps, so walls hide the probes behind them (no light leaking).
- One bounce is added per pass (`bounces`). Probes that mostly see back faces are inside geometry and are marked unusable.
- Surfaces take indirect light from the probes instead of the flat hemispheric ambient. The grid updates when lights or still objects change; objects that moved in the last 2 seconds and skinned meshes are lit by it but not captured.

### Post-processing

Configured by a `PostProcessProfile` asset chosen on a `PostProcessVolume`:

- **Tonemapping:** none, Neutral (Hable's curve, as Unity's) or ACES, plus exposure, then sRGB encoding.
- **Bloom:** downsample/upsample chain (`bloom_down`, `bloom_up`).
- **Vignette.**
- **SSAO:** half-resolution, darkens only the *indirect* share of the light. On by default; `ambient_occlusion = 0` in the profile, or `ssao_intensity = 0` in the project's `[render]` section for maps without a volume, turns it off. `ao_radius` sets its reach.
- **FXAA:** on by default; `antialiasing = false` or `fxaa = false` in `[render]` turns it off.
- **Outline:** a layer-based outline from a mask pass (`outline_mask.ps`).

### Shader notes

- Descriptor set 0 holds per-frame resources (shadow maps, probe maps, draw data); set 1 holds the bindless texture array. Binding numbers are documented at the top of `model_common.hlsli` and `lighting.hlsli`.
- C++ structs such as `CullData` (`vk_scene_renderer.h`) must match their HLSL counterparts.
- After editing shaders, rebuild both Debug and Release trees; Debug (`-Od`) output can produce invalid SPIR-V.

## Scripts

A script is a C++ class deriving from `Mercury::Behaviour`, registered with `MERCURY_SCRIPT`:

```cpp
class Ball : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override { r.Field("speed", speed); }
  void OnStart() override;                          // all objects exist
  void OnTick(double dt) override;                  // fixed rate, before physics
  void OnFrame(double dt) override;                 // every frame; ImGui dev windows allowed
  void OnCollisionEnter(Mercury::GameObject other) override;
  float speed = 60.0f;
};
MERCURY_SCRIPT(Ball);
```

Other callbacks follow Unity: `Awake`, `OnEnable`/`OnDisable`, `OnLateFrame` (LateUpdate), `OnTriggerEnter/Stay/Exit`, `OnCollisionEnter/Stay/Leave` and `OnDestroy`. Inside `OnTick`, `Time::GetDeltaTime()` is the fixed step, as in FixedUpdate.

Scripting API:

- **Physics:** `Physics().AddForce(GetEntity(), force)` (also `AddImpulse`, `AddTorque`, `SetAngularVelocity`) and `Physics().Raycast(origin, direction, distance, layer_mask)`.
- **Objects:** `GetTransform().SetRotate(...)`, `Find`, `FindWithTag`, `FindObjectsOfType<T>()`, `Instantiate(prefab, ...)`, `CreateGameObject`, `AddComponent`, `SetActive`/`SetLayer`, `Destroy(object)`.
- **Maps:** `LoadMap("res://maps/level2.mmap")` replaces the scene at the start of the next frame (like `SceneManager.LoadScene`).
- `Mathf`, `Random` and `Time` match Unity's.

An `Event` field is a UnityEvent: the map saves calls to other scripts' methods, which those scripts list in `ReflectMethods`, and code can `AddListener`:

```cpp
void Reflect(Mercury::Reflector& r) override { r.Field("entered", entered); }
void ReflectMethods(Mercury::MethodReflector& m) override {
  m.Method("TakeDamage", [this](float amount) { TakeDamage(amount); });
}
Mercury::Event entered;  // entered = [[NodePath("Player"), "Health", "TakeDamage", 15.0]]
```

`MERCURY_DATA_ASSET(Type)` registers a reflected struct as a `.masset` type (a ScriptableObject); load one with `Assets().LoadAs<Type>(path)`. `GetInputActions()` returns the project's input actions: `Find("Player/Jump")`, the `performed`/`started`/`canceled` signals and `ReadValue<glm::vec2>()`. With a generated header (Input Actions window, **Code > Generate C++ Header**), construct it from the asset and use members instead of names, so a renamed action fails to compile: `GameInput input(*GetInputActions()); input.Player.Jump->WasPressedThisFrame();`. Actions update once per frame, so read presses in `OnFrame` and keep them for `OnTick` (see `examples/roll_a_ball/scripts/player_controller.cc`).

Fields listed in `Reflect` appear in the Inspector and are saved in maps. Supported types: bool, int, float, std::string, glm::vec2/vec3, glm::vec4 (a color), `NodeRef`, `AssetRef` and enums (`r.Enum(...)`). The editor never loads game code: after each build it runs `game --dump-schema .mercury/scripts.mschema` to learn the scripts' fields. `.mercury/` holds generated files and should not be committed.

## UI Toolkit

In-game UI (HUDs, menus, inventories) uses Mercury's UI Toolkit, a copy of Unity's: layouts in **UXML**, styles in **USS**, a tree of `VisualElement`s laid out with flexbox (Yoga) and drawn by the engine's renderer with SDF text. Dear ImGui is for the editor and developer/debug windows only, not for what players see.

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

Add a `UIDocument` component to a node (`source_asset` is the `.uxml`, `panel_settings` an optional `PanelSettings` asset for scaling and theme), then in a script:

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

Controls: Label, Button, Toggle, RadioButton, Slider, SliderInt, ProgressBar, TextField, IntegerField, FloatField, ScrollView, ListView, Foldout, GroupBox, Image. Also `Q<T>()`/`Query<T>()`, trickle-down and bubbling events (`RegisterCallback<ClickEvent>`, `ChangeEvent<T>`, pointer, keyboard, navigation, focus), USS selectors with `:hover`/`:active`/`:focus`/`:checked`/`:disabled`, variables, transitions, 9-slicing, rich text, and `generateVisualContent` with a `Painter2D` for custom drawing. The default look is `mercury/ui/resources/default_theme.uss`, after Unity's runtime theme.

**Window > UI Builder** (or double-clicking a `.uxml`) edits documents visually: style sheets and selectors, the element hierarchy, a control library to drag from, a live canvas drawn by the game's renderer (with a Preview mode) and an Inspector for attributes, classes, bindings and styles. **+ Create** in the Project panel makes new UI documents, style sheets and panel settings.

**Window > Input Actions** (or double-clicking a `.minput`) edits input actions like Unity's Input Actions editor: action maps on the left, the selected map's actions with their bindings and 2D vector composites in the middle (right-click an action to add either), and the selection's properties on the right. A binding's control path can be typed, picked from the list (with search), or set with **Listen** by pressing the key or gamepad button. **Save** (Ctrl+S) writes the `.minput` and, if **Code > Generate C++ Header** is on, a header with a struct per action map (rewritten only when it changes). **Use as Project Actions** sets `[input] actions` in the `.mproj`. **+ Create > Input Actions** makes a file with Unity's default Player map (Move, Look, Jump, Fire); the first one becomes the project's actions.

## Shipping a game

**Project > Build Game...** makes a folder you can ship, like Source 2's release builds with `.vpk` files. It compiles a Release executable in a separate build under `.mercury/build-release` and packs the assets into a few archives instead of thousands of loose files:

```
Builds/Pong/pong          the game (pong.exe on Windows)
Builds/Pong/engine.mpack  compiled shaders, the default UI theme and fonts
Builds/Pong/engine.mindex index: hash table of path -> archive, offset and size
Builds/Pong/game.mpack    the project: .mproj, maps, assets, UI, textures, models
Builds/Pong/game.mindex   index of game.mpack and the archives below
                          (game_001.mpack, game_002.mpack, ... past the chosen size)
```

- The dialog's "Split game.mpack into files of" option (single file, 100 MB, 256 MB, 512 MB, 1 GB, 2 GB; 256 MB default) is remembered in the editor settings as `pack_size_mb`.
- The output folder is saved in the `.mproj` as `[build] output`.
- Scripts, `CMakeLists.txt`, source files and hidden folders such as `.mercury/` and `Builds/` are left out.
- Each file is compressed with zlib unless that saves under 5% (PNGs and fonts are stored as is), and has a CRC-32 checked on read.
- When the executable starts without a project argument and finds `game.mpack` next to itself, it runs from the packs. Everything else (editor, Play, Build, tests) reads loose files.

The format is described in `mercury/pack/mpack.h`. The reader and the VFS that mounts packs (`mercury/os/filesystem.h`) are in the engine; the packing step is in `editor/release_build.cc`.

## Importing a Unity project

`scripts/unity_import/unity_import.py` converts a Unity project's scenes, prefabs, materials, ScriptableObjects, animator controllers, input actions, UXML/USS, panel settings and volume profiles to Mercury files, and copies models, textures, sounds and fonts. Unity's left-handed axes become Mercury's by mirroring Z. Scripts are ported by hand; a Python script map tells the importer which C++ script takes each C# script and how to convert its fields. `project-iso-mercury/import/run_import.sh` is a complete example, and `python3 -m unittest discover -s scripts/unity_import` tests the conversions.

## Testing

- `ctest --test-dir build` runs unit tests for the formats, maps, reflection, primitive meshes, rotations, `.mpack` archives, the UI Toolkit (USS, UXML, cascade, layout, events, bindings, drawing) and gameplay (events, triggers, raycasts, CharacterController, prefabs, models, animator controllers, input scripts).
- `mercury_editor_smoke <project> <script>` replays scripted input into the editor.
- `MERCURY_INPUT_SCRIPT=walk.txt` does the same for a game (`120 press w`, `200 look 30 0`, `240 tap f`, `260 capture shot.png`, `300 quit`; see `mercury/input/input_script.h`), stepping exactly 1/60 s per frame so runs repeat.
- `MERCURY_CAPTURE=frame.png` (plus `MERCURY_CAPTURE_FRAME=n` and `MERCURY_CAPTURE_QUIT=1`) on any Mercury program saves a screenshot of a frame.
