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

Mercury is a C++ engine for producing games, covering windowing, input, rendering, physics, audio, UI and memory. Game logic is written in AngelScript, run by an embedded AngelScript engine. Its design has three layers:

- **Application** (`Mercury::Application`) handles the platform-level work: creating the window and taking mouse, keyboard and controller input. Platform-specific code is kept to as little of the codebase as possible.
- **Scenes** (`Mercury::GameScene`) are a state of the game. They manage objects, their properties and physics, and know nothing about drawing or windowing.
- **Primitives** are the minimum a scene must hand over to get an object on screen. The Application turns them into buffers and draws them. How primitives are batched or pooled (for example one texture atlas to cut draw calls) is up to the scene.

Source layout under `mercury/`: `application` (Filament renderer), `materials` (Filament `.mat` definitions), `scene`, `components`, `physics_engine`, `animation`, `audio`, `input`, `ui`, `scripting`, `serialization`, `project`, `pack` and `os`.

## Projects and the editor

Games are projects, like in Unity and Godot, rather than hand-written `GameScene`s. `examples/pong` (2D) and `examples/roll_a_ball` (3D, after Unity's tutorial) are complete examples; `examples/breakout` and `examples/space_invaders` are the classic arcade games built the same way. `examples/monkey_ball` is a Super Monkey Ball style stage with a HUD and pause menu. `examples/cpp_hello` is the same idea as the smallest AngelScript example, but its gameplay component is C++. Every example has a replay in `tests/`.

```
pong.mproj              project settings (name, main map, window size)
maps/main.mmap          the map: nodes and their components
assets/*.masset         materials and physics materials
scripts/*.as            game logic in AngelScript (optional when scripts are C++)
scripts/*.cc            game logic in C++ (optional; see C++ scripts)
.mercury/               generated: scripts.mschema and build caches (not committed)
```

There is no per-game build file: every project runs with the same executable, `mercury_player`.

Open a project with the editor:

```sh
./build/release/bin/mercury_editor examples/pong/pong.mproj
# or no argument for the project launcher
```

The editor has a Hierarchy, Inspector, Scene view, Project browser, Console and a built-in code editor. Double-click a `.as` file (or use **Open** in the Project Browser) to edit it in the built-in editor, which has line numbers, folding, AngelScript completion across the whole project and the native API, compiler-backed diagnostics, an outline for jumping to methods, and optional Vim keybindings. **File > Preferences** switches between the built-in editor and an external editor command; the external command supports `{project}` and `{file}` placeholders. On macOS Debug builds, Preferences also has **Debug Play FPS**, which launches Play with `MTL_HUD_ENABLED=1` so Metal's frame-rate and GPU statistics overlay is visible over the game. Lines in the Console that name a script (`scripts/ball.as:42:5`, from a script error) open that file when clicked.

- **Creating nodes:** GameObject menu, **+ Create** in the Hierarchy, or right-click a node for a child. You can add empty nodes, 3D objects (cube, sphere, capsule, cylinder, plane, quad, each with a matching collider), 2D sprites, directional lights and cameras. New projects from the launcher start from a 2D or 3D template.
- **Scene view:** opens in 2D or 3D to suit the map (the **2D** button switches). Click to select, drag to move (along the ground in 3D), **Move / Rotate / Scale** handles on W / E / R, Ctrl to snap. F frames the selection.
  - 2D: right or middle drag pans, wheel zooms.
  - 3D: right drag looks around (W/A/S/D/Q/E to fly, Shift for speed), Alt + left drag orbits, middle drag pans, wheel zooms.
- **Play** saves the map, builds the scripts and runs the game as its own process (`mercury_player <project> --map <map>`), with output in the Console. **Build** only builds the scripts, which refreshes the script list the Inspector uses. There is no hot reload: Play restarts the game.

Run a project without the editor with `./build/release/bin/mercury_player examples/pong/pong.mproj [--map res://maps/x.mmap]`. It compiles the scripts first if they changed. `--build-scripts` only compiles and `--dump-schema file` writes the scripts' fields.

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
- `.minput`: input actions (Unity's Input System): `[action name="Player/Jump" type="button"]` with `bindings` such as `"<Keyboard>/space"` and `"<Gamepad>/buttonSouth"`, and 2D vector `composites`. `[map name="Player"]` sections list the action maps, and the `[minput]` header's `class` and `header` name the generated AngelScript namespace and its file. The `.mproj` names it in `[input] actions`. Edit it in **Window > Input Actions**.
- `.mimport` next to a model: `convention = "unity"` imports it the Unity way (180 degrees about Y, file units); `[import] model_convention` in the `.mproj` sets the default. `scale` resizes it and `max_texture_size` (default 8192) scales larger textures down.

Models bring their own materials: base color and texture, opacity, emission (`Ke`, `map_Ke`), normal maps (or `bump` height maps, converted to normal maps on import) and, for OBJ, glossiness from `Ns`/`Ks`. Texture names are matched case-insensitively.

`.mproj` sections: `[physics]` (`gravity = Vector3(0, -9.81, 0)`, `tick_rate = 50`), `[layers] names`, `[tags] names` (set on a node with `layer=` and `tag=`), `[render]` (SSAO and outline settings), `[input]`, `[import]` and `[build]` (game output folder, and `target = "//pkg:game"` when the gameplay is C++).

## Components

A node's position is relative to its parent; maps are flattened to world positions when loaded, and rotation and scale are not inherited. Shapes have unit size at scale 1 (1x1x1 cube, sphere 1 across, capsule 1 across and 2 tall), like Unity's. Asset types are `Material`, `PhysicsMaterial`, `PanelSettings`, `RenderTexture`, `PostProcessProfile` and any data assets scripts declare.

| Component | What it does |
|---|---|
| `Transform` | `position`, `rotation` (Euler degrees, applied X, then Y, then Z) and `scale` |
| `SpriteRenderer` | An unlit quad on the XY plane, sized by the scale (2D) |
| `MeshRenderer` | A lit `mesh`: `cube`, `sphere`, `capsule`, `cylinder`, `plane`, `quad`, or `model` with a `.obj`/`.gltf`/`.glb`/`.fbx` file (node tree, materials, skin and clips; `node` draws one part, `materials` overrides them) |
| `Camera` | Looks along its -Z. `fit_height` makes a 2D camera; also `fov`, clip distances, `background`, `skybox` + `skybox_intensity`, `clear`, `culling_mask`, `depth`, `overlay` (drawn over `base_camera`), `target_texture`, `post_processing` |
| `DirectionalLight` | Sunlight along its -Z: `color`, `intensity`, `ambient`, `ground_ambient`, `cast_shadows` (see [Lighting](#lighting)) |
| `PointLight` | Light in every direction: `color`, `intensity`, `range` (Unity's falloff; up to 16 per map). Soft shadows that widen with `radius`; `shadows = "hard"` or `"none"` changes that |
| `AreaLight` | A glowing rectangle of `size` in the node's XY plane, shining along -Z. `color` * `intensity` is its radiance. The Filament renderer approximates it with a spot light; up to 4 per map |
| `LightProbeVolume` | A room-capture light probe at the node: the renderer draws the scene from it into a cubemap and lights everything with it, so mirrors reflect the room and walls bleed color. `bounces` (captures), `intensity`, `capture_size` (see [Lighting](#lighting)) |
| `Rigidbody` | Static, kinematic or dynamic motion, `mass`, locked axes, damping, gravity |
| `BoxCollider`, `SphereCollider`, `CapsuleCollider`, `CylinderCollider`, `PlaneCollider`, `MeshCollider` | Collision shapes, scaled by the Transform, with a `center`; a node may have several. `is_trigger` reports overlaps without pushing |
| `CharacterController` | Unity's: `Move(displacement)`, `IsGrounded()`, slopes and steps, resizable at run time |
| `Animator` | Plays a `.manim` controller (parameters, blend trees, transitions, layers) on the node's model |
| `AudioSource`, `AudioListener` | Sound playback (miniaudio) with 3D attenuation; `Play`, `Stop`, `PlayOneShot` |
| `TextMesh` | SDF text in the world (TextMeshPro 3D) |
| `PostProcessVolume` | Picks a `PostProcessProfile` asset (see [Post-processing](#post-processing)) |
| `UIDocument` | Shows a `.uxml` document on screen (see [UI Toolkit](#ui-toolkit)) |

## Rendering

Mercury renders with [Filament](https://github.com/google/filament) (Google's
PBR engine), on its Vulkan backend. Filament owns the GPU: device, swapchain,
pipeline state, shadow maps and post-processing are the library's, so the
engine only translates its own world into Filament objects.

- `mercury/application/filament_application.{h,cc}` — the `Application` and
  `RenderHost` implementation: Engine/Renderer/SwapChain lifetime, one `View`
  per output (window, editor viewport, camera preview, UI Builder canvas) and
  the pixel readbacks the editor copies out.
- `mercury/application/filament/filament_renderer.{h,cc}` — mirrors the ECS
  into a Filament `Scene`: `RenderMesh` draws become renderables (with skins),
  `ImageData` becomes textures, `SceneLighting` becomes a sun, point lights and
  a constant ambient `IndirectLight`.
- `mercury/application/filament/filament_ui.{h,cc}` — draws
  `UI::UIDrawData` with Filament (one renderable per command, textures from
  `UITextureRegistry`).
- `mercury/materials/*.mat` — Mercury's material definitions. `matc`
  (built by `//third_party:filament_build`) compiles them to `.filamat` at
  build time; the player and editor read them from `mercury/materials` (or
  from `engine.mpack` in a shipped build).

### Frame outline

1. `FilamentRenderer::SyncScene` reconciles the registry: new/changed
   `RenderMesh` entities are rebuilt, transforms and bones updated, lights
   rebuilt when their values changed.
2. The main camera becomes the window's scene view; the game's
   `UI::UIDrawData`, when there is one, is drawn by a second view over it
   (`BlendMode::TRANSLUCENT`).
3. Editor outputs render into `RenderTarget`s (colour + depth textures) and
   are copied to the CPU with `Renderer::readPixels`; `CopyScenePixels`,
   `CopyPreviewPixels` and `CopyUIPreviewPixels` hand those buffers to the
   editor.
4. Filament's own passes follow: shadow maps, GTAO/SSAO, bloom, tonemapping
   and anti-aliasing are all library features enabled through `View` options.

### Lighting

Materials are Filament's physically-based `lit` model (metallic/roughness,
image-based ambient). `SceneLighting` maps onto:

- **Directional light** → a Filament `SUN` (colour × intensity in lux), with
  shadow maps.
- **Point lights** → Filament `POINT` lights (lumens), with per-light shadow
  bias and bulb radius.
- **Area lights** → a Filament `SPOT` aimed along the rectangle's normal.
  Filament has no rectangle light; the spot is the native approximation.
- **Ambient (`ambient`, `ambient_ground`)** → a constant ambient
  `IndirectLight` built from spherical harmonics; the two colours are
  averaged, so the hemispheric gradient the old renderer had is gone.
- **Skybox panoramas** (`.png`, `.jpg`, `.hdr`) → prefiltered with Filament's
  `IBLPrefilterContext` into a `Skybox` and an `IndirectLight` with specular
  reflections and irradiance. `skybox_intensity` is the `IndirectLight`
  intensity in lux; `clear = "color"` keeps the panorama out of the frame
  while still using it for image-based light and reflections.
- **Soft shadows** → Filament's PCSS (`View::setShadowType`); the light's
  physical size (`radius`, or the area light's rectangle) sets the penumbra.

- **`LightProbeVolume`** → a room-capture probe. The renderer draws the
  scene from the node's position into the six faces of a cubemap (six
  Filament `View`s at 90 degrees, rendered with `renderStandaloneView`),
  filters it with `IBLPrefilterContext` (specular and irradiance) and sets
  the result as the scene's `IndirectLight`. Each of `bounces` captures is
  lit by the one before it. While a probe is in the map it replaces the
  skybox's and the ambient's image-based light (a skybox can still be drawn
  as the background); the ambient only reaches the final image through the
  captured bounces, so raise the probe's `intensity` to compensate. It is
  captured once the scene has been still for a few frames, and again after
  the scene, its lights or the probe change.

### Post-processing

`RenderSettings`/`PostProcessProfile` are applied to each Filament `View`:
tonemap and exposure through `ColorGrading`, bloom, vignette, GTAO,
screen-space reflections (mirrors and glossy surfaces), and PCSS soft
shadows.

Anti-aliasing and upscaling are project options in the `.mproj`'s `[render]`
section (the editor's Build Game dialog writes them), not part of a profile:
`anti_aliasing = "none" | "fxaa" | "taa"` (default `taa`; a legacy `fxaa`
bool is read only when `anti_aliasing` is absent), and the optional AMD FSR 1
upscaler, `fsr = true`, with `fsr_quality = "high" | "ultra"`,
`render_scale` (0.5 to 1, default 0.75) and `fsr_sharpness` (0 to 1, default
0.9). FSR applies to the window's scene view and the editor's scene viewport
only; render textures, previews and the UI stay at native resolution. TAA
adds Filament's motion vectors pass and runs only on the main scene views;
other views (render textures, previews) use FXAA when the project chose TAA.
FSR 1 expects a well anti-aliased image, so pair it with TAA.

The default anti-aliasing is now TAA (it used to be FXAA), and a
`PostProcessProfile`'s `antialiasing` key is no longer read; anti-aliasing
comes only from the project. Filament's motion vectors do not capture skinned
meshes, so animated characters rely on TAA's neighbourhood clamping and may
show slight smearing.

Refraction uses Filament's cubemap mode so it also works on
MoltenVK, where the screen-space refraction pass currently draws nothing.

### Material notes

- `lit_opaque`, `lit_opaque_double_sided`, `lit_transparent`,
  `lit_transparent_double_sided`, `unlit` and `text_sdf` are the shipped
  materials. culling and blending are compile-time material properties, which
  is why each combination exists.
- Parameters set per draw: `baseColor` (linear), `baseColorMap` (always
  bound — Filament panics on unbound samplers), `tiling`, `offset`,
  `metallic`, `roughness` (`1 - smoothness`) and `emissive`.
- Material variables (`uiParams`, `uiOutline` in `ui.mat`) come from custom
  vertex attributes (`CUSTOM0`, `CUSTOM1`); the vertex shader copies them with
  `getCustom0()` so the fragment can read `variable_*`.

## Scripts

Gameplay is written in either AngelScript or C++. Both use the same component model, reflection and
map format: a `[script]` AngelScript class and a C++ `MERCURY_SCRIPT` type are both registered in the
`TypeRegistry`, both show their `Reflect` fields in the Inspector, and both are saved in a `.mmap` by
name. A project may use both at once.

### AngelScript

Scripts are AngelScript classes in `scripts/` (any `.as` file). The engine compiles every file with
`CScriptBuilder` in-process at Build/Play time, so there is no external bundler, source map or type
checker. A class marked `[script]` is registered as a component; a class marked `[dataAsset]` is a
ScriptableObject-style asset type.

```angelscript
[script]
class Ball : Behaviour
{
    [field tooltip="World units per second"]
    float speed = 60.0f;

    void onStart() {}                       // all objects exist
    void onTick(double dt) {                // fixed rate, before physics
        GameObject self = GetGameObject();
        self.position = self.position + Vector3(0.0f, 0.0f, 1.0f) * speed * dt;
    }
    void onFrame(double dt) {}              // every frame
    void onCollisionEnter(GameObject other) {}
}
```

Add it to a node in the editor, or with `[component type="Ball" node="..."]` in a `.mmap`. A file
can hold several classes. The map name is the class name; `[script "Name"]` pins it.

**Lifecycle** (the names are Unity's, camelCase): `awake`, `onEnable`/`onDisable`, `onStart`,
`onFrame` (Update), `onLateFrame` (LateUpdate), `onTick` (FixedUpdate), `onTriggerEnter/Stay/Exit`,
`onCollisionEnter/Stay/Leave` and `onDestroy`. Define only the ones you use. Inside `onTick`,
`Time::FixedDeltaTime()` is the fixed step. **Do not call engine APIs from a constructor**, only
initialise fields there (the object does not exist yet); use `awake` or `onStart`.

**Metadata** replaces decorators:

| Metadata | Meaning |
|---|---|
| `[script]` / `[script "Name"]` | Register the class as a component (and optionally override its map name) |
| `[field]` | Inspector field, saved in the map. The type comes from the declaration: `bool`, `int`, `float`, `string`, `Vector2`, `Vector3`, `Color`, `Event` and `array<T>` work |
| `[field tooltip="..."]` | The same, with a tooltip |
| `[enum "a", "b"]` | An `int` field shown as a drop-down; the map stores the name |
| `[nodeRef]` | A `string` field edited as a node path |
| `[asset "Material"]` | A `string` field edited as an asset path of one type |
| `[bindable]` on `get_x()` | A computed value UI bindings may read, not saved or shown in the Inspector |
| `[callable]` | A method an `Event` saved in the map may call (its argument type comes from the signature: none, `bool`, `int`, `float` or `string`) |
| `[dataAsset]` | A ScriptableObject-style `.masset` type |

The map key is the property name exactly (`minHorizontal`, not `min_horizontal`). A key in a map that
no field has is reported as a warning, which catches renamed fields.

**The API** is plain AngelScript. Native functions and value types live in the default namespace
(`GetGameObject`, `GetBehaviour`, `Scene`, `Input`, `Physics`, `Mathf`, `Random`, `Time`, `Debug`,
`Components`, `UI`, `Assets`); nothing can reach the engine's internals.

- **Objects:** `GetGameObject()` returns a `GameObject` with `name`, `path`, `tag`, `layer`,
  `activeSelf`, `activeInHierarchy`, `parent`, `children`, `position`, `rotation`, `eulerAngles`,
  `localScale`, `velocity`, `isValid`, `hasBounds`/`boundsMin`/`boundsMax`, and methods `FindChild`,
  `SetParent`, `Destroy`.
- **Scenes:** `Scene::Find`, `FindWithTag`, `FindAllWithTag`, `FindObjectsWithScript`,
  `Instantiate(prefab, position, rotation, parent)`, `CreateGameObject`, `LoadMap("res://maps/level2.mmap")`
  (applied at the start of the next frame) and `Scene::Quit()`.
- **Other scripts:** `GetBehaviour(object, "Ball")` returns an `IBehaviour@`; cast it to the concrete
  class: `Ball@ ball = cast<Ball>(GetBehaviour(object, "Ball"));`.
- **Vectors are values.** `Vector2`, `Vector3`, `Quaternion` and `Color` are native value types with
  operators and methods (`Length`, `Normalized`, `Scale`, `Dot`, `Cross`, `Slerp`). Assign a whole
  vector: `object.position = object.position + Vector3(0.0f, 1.0f, 0.0f);`.
- **Components:** `Components::Get(object, "Rigidbody")`, `Has`, `Add`, `Remove`. The returned
  `Component` reads and writes its fields with `GetFloat`/`SetFloat`, `GetBool`/`SetBool`,
  `GetInt`/`SetInt`, `GetString`/`SetString`, `GetVector2`/`SetVector2`, `GetVector3`/`SetVector3`
  and `GetColor`/`SetColor`. Typed helpers cover the interactive components:
  `AnimatorSetFloat`/`AnimatorPlay`, `AudioPlay`/`AudioPlayOneShot`/`AudioSetFloat`,
  `CharacterMove`/`CharacterGrounded`.
- **Physics:** `Physics::AddForce(object, force, ForceMode::Force)`, `AddImpulse`, `AddTorque`,
  `SetAngularVelocity` and `Raycast(origin, direction, maxDistance, layerMask, queryTriggers, hit, point, normal, distance)`.
- **Input:** `Input::IsKeyPressed(Key::W)`, `IsKeyJustPressed`, `IsMouseButtonPressed`,
  `CursorDelta()`, `Scroll()`, `IsPointerOverUI()`. Actions come from the project's Input System
  asset: `InputAction jump = Input::RequireAction("Player/Jump"); jump.wasPressedThisFrame`. The
  Input Actions window (**Code > Generate AngelScript**) writes a namespace with constants, so a
  renamed action fails to compile: `Input::RequireAction(GameInput::Player::Jump)`. Actions update
  once per frame, so read presses in `onFrame` and keep them for `onTick` (see
  `examples/roll_a_ball/scripts/player_controller.as`).
- **UI:** `UI::Root()` returns the `VisualElement` of this object's `UIDocument`; then
  `Q("name")`, `QClass("class")`, `SetText`, `get_text`/`set_text`, `SetClass("name", on)`,
  `HasClass`, `Focus`, `BlurFocused`, `SetDisplay`, `OnClick("Method")`/`OnClick("Method", "arg")`,
  `OnToggleChanged("Method")`, and `SetDataSource()` to bind the UXML to your `[field]`s and
  `[bindable]` getters.
- `Mathf`, `Random` and `Time` match Unity's (`Mathf::Clamp`, `Random::Range`, `Time::DeltaTime()`).
  `Debug::Log/Warn/Error/Info` write to the Console.
- **Callbacks are method names.** UI, timers and events name a method instead of capturing a
  closure: `button.OnClick("BackToMenu")`. Keep per-callback state in fields.

An `Event` field is a UnityEvent: the map saves calls to other scripts' `[callable]` methods:

```angelscript
[field] Event entered;                     // entered = [[NodePath("Player"), "Health", "TakeDamage", 15.0]]
void onTriggerEnter(GameObject other) { entered.Invoke(); }

// in Health:
[callable] void TakeDamage(float amount) { ... }
```

**Errors.** A script that throws does not stop the game: the error is logged with its position
(`scripts/ball.as:42:5`) and the next call still runs. A call that runs longer than
`MERCURY_SCRIPT_TIMEOUT` seconds (default 10, 0 turns it off) is aborted as an infinite loop. A
destroyed object throws `GameObject was destroyed` when used; check `isValid` if it may be gone.
Scripts only hold opaque handles (a scene serial and an entity id), never pointers, so none of this
can crash the engine. The engine is compiled with AngelScript's unsafe-reference support off and
registers no raw pointers.

**Type checking.** The compiler checks every script on Build and Play; a type error fails the
build. There is no separate language-server step. `.mercury/` holds the generated schema and build
stamps and should not be committed. The editor never runs game code: after each build it runs
`mercury_player <project> --dump-schema .mercury/scripts.mschema` to learn the scripts' fields.

### C++ scripts

A project can also compile C++ gameplay into its own executable. Derive from `Mercury::Behaviour`,
describe fields in `Reflect`, and register the class with `MERCURY_SCRIPT` in its `.cc` file:

```cpp
#include "scripting/behaviour.h"

class Spinner : public Mercury::Behaviour {
 public:
  void Reflect(Mercury::Reflector& r) override {
    r.Field("speed", speed).Tooltip("Degrees per second");
  }
  void OnFrame(double dt) override {
    GetTransform().ChangeRotate({0.0f, speed * static_cast<float>(dt), 0.0f});
  }
  float speed = 90.0f;
};

MERCURY_SCRIPT(Spinner);
```

The callback names are the C++ ones (`Awake`, `OnStart`, `OnFrame`, `OnLateFrame`, `OnTick`,
`OnCollisionEnter`, ...). `MERCURY_SCRIPT` runs at static initialization, before the player loads a
map. A reflected struct can be registered as a `.masset` type with `MERCURY_DATA_ASSET(T)` and loaded
with `Assets().LoadAs<T>(path)`.

C++ scripts and AngelScript classes use the same map component name and field names, so a node can
mix them. Unlike AngelScript, C++ code is linked into the executable: the shared `mercury_player`
cannot load it. Build a game executable with the `mercury_add_game` CMake function:

```cmake
# my_game/CMakeLists.txt
mercury_add_game(my_game PROJECT "${CMAKE_CURRENT_SOURCE_DIR}")
```

`mercury_add_game` compiles every `.cc`/`.cpp` under `scripts/` and generates a `main()` that calls
`Mercury::RunPlayer`. Name the target in the project file:

```ini
[build]
target = "my_game"
```

Projects in the engine checkout add their directory from the top-level `CMakeLists.txt` (as
`examples/cpp_hello` does); an external project can `add_subdirectory` the engine checkout and call
the function from its own `CMakeLists.txt`. The editor's **Build Scripts** and **Play** then run
`cmake --build <build-dir> --target <target>`, ask the executable for its script list with
`--dump-schema`, and launch it. Projects without `[build] target` keep using the shared player and
its AngelScript runtime. `examples/cpp_hello` is a complete C++ project; build and run it with
`cmake --build --preset release` and `./build/release/bin/cpp_hello`.

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

**Window > Input Actions** (or double-clicking a `.minput`) edits input actions like Unity's Input Actions editor: action maps on the left, the selected map's actions with their bindings and 2D vector composites in the middle (right-click an action to add either), and the selection's properties on the right. A binding's control path can be typed, picked from the list (with search), or set with **Listen** by pressing the key or gamepad button. **Save** (Ctrl+S) writes the `.minput` and, if **Code > Generate AngelScript** is on, an AngelScript namespace with a constant per action (rewritten only when it changes). **Use as Project Actions** sets `[input] actions` in the `.mproj`. **+ Create > Input Actions** makes a file with Unity's default Player map (Move, Look, Jump, Fire); the first one becomes the project's actions.

## Shipping a game

**Project > Build Game...** makes a folder you can ship, like Source 2's release builds with `.vpk` files. In a development editor it builds a Release `mercury_player` from the engine checkout; a released editor reuses the Release player bundled with it. It minifies the scripts and packs the assets into a few archives instead of thousands of loose files:

```
Builds/Pong/pong          the game: a copy of mercury_player (pong.exe on Windows)
Builds/Pong/engine.mpack  compiled materials, the default UI theme and fonts
Builds/Pong/engine.mindex index: hash table of path -> archive, offset and size
Builds/Pong/game.mpack    the project: .mproj, maps, assets, UI, textures, models and
                          the minified .mercury/scripts/game.js (+ game.js.map)
Builds/Pong/game.mindex   index of game.mpack and the archives below
                          (game_001.mpack, game_002.mpack, ... past the chosen size)
```

- The dialog's "Split game.mpack into files of" option (single file, 100 MB, 256 MB, 512 MB, 1 GB, 2 GB; 256 MB default) is remembered in the editor settings as `pack_size_mb`.
- The output folder is saved in the `.mproj` as `[build] output`.
- The `.as` sources are packed (they are tiny); hidden folders such as `.mercury/` and `Builds/` are left out.
- Each file is compressed with zlib unless that saves under 5% (PNGs and fonts are stored as is), and has a CRC-32 checked on read.
- When the executable starts without a project argument and finds `game.mpack` next to itself, it runs from the packs. Everything else (editor, Play, Build, tests) reads loose files.

The format is described in `mercury/pack/mpack.h`. The reader and the VFS that mounts packs (`mercury/os/filesystem.h`) are in the engine; the packing step is in `editor/release_build.cc`.

On macOS, `scripts/package_macos.pl` wraps that output in a self-contained `.app`:

```sh
./scripts/package_macos.pl --project examples/pong/pong.mproj
open Builds/Pong.app
```

It builds the Release player with CMake, writes `game.mpack`/`engine.mpack` (the same format, with the same split, compression and CRC rules as Build Game) and puts them in `Contents/Resources`. The player, the Vulkan loader (`libvulkan`) and MoltenVK are copied into the bundle, their install names are rewritten to `@rpath`, and a relative `vulkan/icd.d/MoltenVK_icd.json` tells the loader to use the bundled driver, so nothing points at the checkout or Homebrew. The bundle is ad-hoc signed by default; pass `--identity "Developer ID Application: ..."` for a distributable, hardened-runtime signature and `--zip` to also produce a zip for `ditto`/notarization.

`--target editor` packages the editor instead, as a complete development and packaging install:

```sh
./scripts/package_macos.pl --target editor --name "Mercury Editor" --zip
```

Its bundle contains the editor and `mercury_player` in `Contents/MacOS`, the loose UI theme/fonts and compiled materials in `Contents/Resources/ui` and `Contents/Resources/materials`, the Vulkan loader and MoltenVK, and the engine's static libraries in `Contents/Resources/engine/lib`. Play therefore runs the bundled player, and Build Game packages a game without a compiler or the engine checkout: it reuses the bundled Release player and engine resources, then copies the player's runtime libraries (`Contents/Resources/engine/player-libs.txt`) and a `vulkan/icd.d` manifest next to the game so the output folder is self-contained. Pass `--no-engine-libs` to leave the static libraries out of an editor bundle.

## Importing a Unity project

`scripts/unity_import/unity_import.py` converts a Unity project's scenes, prefabs, materials, ScriptableObjects, animator controllers, input actions, UXML/USS, panel settings and volume profiles to Mercury files, and copies models, textures, sounds and fonts. Unity's left-handed axes become Mercury's by mirroring Z. Scripts are ported by hand; a Python script map tells the importer which AngelScript script ([script] class) takes each C# script and how to convert its fields. `project-iso-mercury/import/run_import.sh` is a complete example, and `python3 -m unittest discover -s scripts/unity_import` tests the conversions.

## Testing

- `cmake --build --preset check` builds and runs the unit tests for the formats, maps, reflection, primitive meshes, rotations, `.mpack` archives, the UI Toolkit (USS, UXML, cascade, layout, events, bindings, drawing) gameplay (events, triggers, raycasts, CharacterController, prefabs, models, animator controllers, input scripts), the AngelScript runtime (evaluation, errors, value types, the timeout) and scripts as behaviours (`tests/script_behaviour_test.cc`, with fixtures in `tests/scripts/`). `tests/example_projects_test.cc` compiles each example, loads its scripts and checks that every map component, property and resource exists. Each example has an input script in `examples/<name>/tests/` that plays it: `MERCURY_INPUT_SCRIPT=examples/pong/tests/play.input ./build/release/bin/mercury_player examples/pong/pong.mproj`.
- `./build/release/bin/editor_smoke <project> <script>` replays scripted input into the editor.
- `MERCURY_INPUT_SCRIPT=walk.txt` does the same for a game (`120 press w`, `200 look 30 0`, `240 tap f`, `260 capture shot.png`, `300 quit`; a missing folder in a capture path is created; see `mercury/input/input_script.h`), stepping exactly 1/60 s per frame so runs repeat.
- `MERCURY_CAPTURE=frame.png` (plus `MERCURY_CAPTURE_FRAME=n` and `MERCURY_CAPTURE_QUIT=1`) on any Mercury program saves a screenshot of a frame.
