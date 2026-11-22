## What Mercury is

Mercury is a C++ game engine (windowing, input, rendering, physics, audio, UI). A game is a *project*: a folder with a `.mproj` file at its root. Game logic is written in AngelScript, run by an embedded AngelScript engine; gameplay can also be C++. You are connected to the Mercury editor's MCP server, which lets you read and change the project the editor has open.

A project looks like this (optional parts are marked):

```
<name>.mproj            project settings (name, main map, window size, ...)
maps/main.mmap          a map: nodes and their components
assets/*.masset         materials, physics materials and other data assets
scripts/*.as            game logic in AngelScript (optional)
src/*.cc, src/*.h       C++ gameplay and runtime systems (optional)
system/*.cc, *.h        editor extension modules (optional)
.mercury/               generated: script schema, build stamps (do not edit, not committed)
```

## File formats

- `.mproj`, `.mmap` and `.masset` are TOML, "format 2". A file starts with a header table that names the file type and carries `format = 2` and `editor_version`; sections follow as `[[tag]]` array-of-tables, grouped by tag, with section attributes as scalar keys and section properties in a `[tag.props]` sub-table. A file whose `format` is newer than the engine's is rejected; the old format 1 layout is not supported.
- A map example:

  ```toml
  [mmap]
  format = 2
  editor_version = "1.0.0"

  [[component]]
  node = "Ball"
  type = "Transform"
  [component.props]
  scale = { vec3 = [2.0, 2.0, 1.0] }

  [[ext_resource]]
  id = "1"
  path = "res://assets/white.masset"
  type = "Material"

  [[node]]
  name = "Pong"

  [[node]]
  name = "Ball"
  parent = "."
  ```

- In a map, `parent` and `node` are paths from the root node: `.` is the root, `Ball` a child of the root, `Walls/Left` a grandchild. Parents come before their children.
- Values in files are `true`, `3`, `1.5`, `"text"`, `{ vec2 = [x, y] }`, `{ vec3 = [x, y, z] }`, `{ color = [r, g, b, a] }`, `{ path = "Walls/Left" }`, `{ ext = "1" }` (an `[[ext_resource]]` id), `{ res = "res://..." }` or plain arrays. Fields left at their default value are omitted. Comments start with `#` but are not kept when the editor saves.
- `res://` paths are relative to the folder that holds the `.mproj`. Every path you pass to a tool or read from a resource uses `res://`.
- Other files: `.mprefab` (a node tree in the map format; a node with `instance = "res://gun.mprefab"` copies it), `.manim` (an animator controller), `.minput` (input actions: `[[action]]` sections with `bindings` such as `"<Keyboard>/space"`, and `[[map]]` sections for the action maps), `.uxml` and `.uss` (UI documents and style sheets), `.mimport` (import settings next to a model).
- `.mproj` sections: `[[physics]]` (`gravity`, `tick_rate`), `[[layers]]`, `[[tags]]` (a node gets a layer and a tag with `layer =` and `tag =`), `[[render]]`, `[[input]]` (`actions` names the `.minput`), `[[import]]` and `[[build]]`.

## Coordinates and units

- Y is up. The default gravity is `(0, -9.81, 0)`, in units per second squared. A Camera and a DirectionalLight look along their own -Z. The axes are right-handed: Mercury's importer turns Unity's left-handed axes into Mercury's by mirroring Z.
- Distances are in world units. By convention the unit is a metre: the physics treats world units as metres, and the default gravity is 9.81 units per second squared. Nothing enforces a physical unit beyond that. Shapes have unit size at scale 1: a cube is 1x1x1, a sphere is 1 across, a capsule is 1 across and 2 tall.
- A Transform has `position`, `rotation` and `scale`. Rotation is stored as Euler angles in degrees, `(pitch, yaw, roll)` around X, Y and Z, applied X first, then Y, then Z (R = Rz * Ry * Rx).
- The Transform values saved in a map, and the ones the scene tools read and write, are LOCAL: position, rotation and scale are all relative to the node's parent. The world transform composes the parent chain: world position = parent position + parent rotation applied to (parent scale times local position); world rotation = parent rotation composed with the local rotation; world scale = parent scale times local scale. Rotation and scale are therefore inherited by children. A node directly under the root is local to the root node's own Transform.
- In AngelScript, `GameObject.position`, `rotation` and `eulerAngles` are world values, while `localScale` is the local scale.
- In tool arguments and results, values use the introspection encoding: bool, int, float, string and enum as themselves; Vector2 / Vector3 / Color as arrays `[x, y]`, `[x, y, z]`, `[r, g, b, a]`; a NodePath field as `{"path": "Child/Grandchild"}`; a Resource field as `{"res": "res://assets/x.masset"}`; a list as an array.
- A node is named in tool calls as `{"uid": 42}` or `{"path": "Root/Child/Grandchild"}`. The path form starts at the root node's name; the engine's own form without the root (`Child/Grandchild`, `.` for the root) is accepted too.

## Scripts

Gameplay is AngelScript classes in `scripts/` (any `.as` file), compiled in-process when the project is built or played; a type error fails the build. There is no hot reload: Play restarts the game.

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

- `[script]` (or `[script "Name"]`) registers the class as a component; the name in a map is the class name unless `[script "Name"]` pins it. Add it to a node in the editor, or with `[[component]]` `type = "Ball"` in a `.mmap`.
- `[field]` makes a field an Inspector field that is saved in the map; the type comes from the declaration (`bool`, `int`, `float`, `string`, `Vector2`, `Vector3`, `Color`, `Event`, `array<T>`). `[field tooltip="..."]` adds a tooltip. Other metadata: `[enum "a", "b"]` (an `int` drop-down; the map stores the name), `[nodeRef]` (a `string` edited as a node path), `[asset "Material"]` (a `string` edited as an asset path of one type), `[bindable]` on `get_x()`, `[dataAsset]` (a ScriptableObject-style `.masset` type).
- `[callable]` marks a method that an `Event` field saved in the map may call.
- The map key is the field name exactly (`minHorizontal`, not `min_horizontal`). `validate_project` reports an unknown field or enum value on a registered type (a built-in, or a script whose schema has loaded) as `error`, because the loader silently drops it; an unknown component or asset **type** (for example a script whose schema has not loaded) is only `warning`, since it may still be valid.
- Lifecycle (Unity's names, camelCase): `awake`, `onEnable`/`onDisable`, `onStart`, `onFrame`, `onLateFrame`, `onTick` (fixed step, `Time::FixedDeltaTime()`), `onTriggerEnter/Stay/Exit`, `onCollisionEnter/Stay/Leave` and `onDestroy`. Define only the ones you use. Do not call engine APIs from a constructor (the object does not exist yet); use `awake` or `onStart`.
- A script error does not stop the game: it is logged with its position (`scripts/ball.as:42:5`) and the next call still runs. A destroyed object throws when used; check `isValid` if it may be gone.
- C++ gameplay lives in `src/` (a class derived from `Mercury::Behaviour`, registered with `MERCURY_SCRIPT`), uses the same component model and map names, and needs `[build] target` in the `.mproj`. Prefer AngelScript unless the project already uses C++.

## Recommended workflow

1. `get_editor_state`: see what the editor has open before you change anything.
2. `find_nodes` / `get_node`: look at the nodes you are about to touch. Use `get_node` for one node's components and values.
3. `apply_scene_operations`: make the changes, in one batch where you can. Look up component and field names in the `mercury://project/schema` resource instead of guessing; a wrong name comes back as an error with a suggestion.
4. `capture_viewport`: look at the result and check it is what you meant.
5. `validate_project`: load every project file through the engine's own loaders and report parse errors, unknown component or field names and `res://` references to files that do not exist. Fix every error.
6. `save_map`: save the open map to its file.

Tools that are not in the catalogue below are not available in this editor build; use the ones that are. Read tools only look. Write and Exec tools change the project or run code and are refused unless **File > Preferences > MCP** has Access Mode set to "Allow Scene Edits" (the default is Read-Only). A failed call returns `{"error": "...", "suggestion": "..."}` (the suggestion is present when a close name exists). Tools that list things take `limit` (default 100, at most 1000) and report `truncated`.

## Resources

- `mercury://guides/AGENTS.md`, `mercury://guides/CLAUDE.md`, `mercury://guides/GEMINI.md`: this guide, with the agent-specific setup at the top (also the `get_agent_guides` tool). They are never written to disk by the editor.
- `mercury://project/structure`: the project's files by kind (project file, maps, prefabs, scripts, ui, input, animators, data_assets, models, textures, audio), as `res://` paths.
- `mercury://project/schema`: every component, script and asset type the engine knows, with their fields, types, defaults and enum values.
- `mercury://asset/{+path}`: what is inside one file, e.g. `mercury://asset/assets/player.masset`: bounds and clips of a model, the size of a texture, the fields of a `.masset`.

## Tools in this editor

{{tools}}

## This project

{{project}}
