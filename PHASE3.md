# Phase 3 — Finish the Filament integration

This file is a handoff for the next agent. Read it top to bottom before
changing anything.

## Where the code is now

Mercury's renderer went through two phases:

1. **Phase 1** deleted the old hand-written Vulkan renderer: GPU culling, light
   probe GI, SSAO, bloom, custom shadow maps/cubes, transmittance, screen-space
   refraction and every HLSL shader. What was left was a minimal Vulkan
   renderer that drew unlit textures.
2. **Phase 2** replaced that with [Google Filament](https://github.com/google/filament)
   on its Vulkan backend. Filament owns the device, swapchain, pipelines,
   shadow maps and post-processing.

The current state:

| File | Role |
| --- | --- |
| `third_party/filament` | Filament 1.81.0 submodule (Vulkan only, Metal/OpenGL/WebGPU off). |
| `third_party/BUILD.bazel` | `//third_party:filament_build` (CMake via `rules_foreign_cc`, Ninja) and `//third_party:filament`. |
| `tools/matc.bzl`, `//tools:matc` | Compiles `.mat` → `.filamat` at build time. |
| `mercury/materials/*.mat` | `lit_opaque`, `lit_opaque_double_sided`, `lit_transparent`, `lit_transparent_double_sided`, `unlit`, `text_sdf`, `ui`. |
| `mercury/application/filament_application.{h,cc}` | `Application` + `RenderHost`: Engine/Renderer/SwapChain, one `View` per output, offscreen `RenderTarget`s and `readPixels` for the editor. |
| `mercury/application/filament/filament_renderer.{h,cc}` | ECS → Filament: renderables, skins, textures, sun/point lights, ambient `IndirectLight`. |
| `mercury/application/filament/filament_ui.{h,cc}` | `UI::UIDrawData` → Filament renderables (solid, textured, SDF text). |

What already works: windowed player, headless editor viewports (scene +
camera preview + UI Builder canvas), the three editor readback paths, PBR
shading with sun shadows, point lights, ambient light, mipmapped textures,
transparent and double-sided materials, skinning, `MERCURY_CAPTURE`
screenshots, and `bazel test //tests:...` at parity with before (the three
`ModelCacheTest` failures and the `model_viewer` example failure are
pre-existing: those asset folders are not in the repository).

## Known gaps (fix these first)

1. **UI SDF text does not render (some builds: solid blocks).** Established so
   far: the CPU atlas is correct (dumping `UITexture::rgba` shows the glyph
   SDF), `variable_uiParams` reaches the fragment correctly (`params.x == 2`
   for text), the UVs are correct (verified by outputting them as colour), and
   the atlas texture — not the 1x1 white fallback — is bound. What fails is the
   sampled alpha: it comes back constant (1.0 with `RGBA8`, 0.0 with
   `SRGB8_A8`), so the glyph shape never reaches the shader. The *scene*
   texture path (`FilamentRenderer::UploadTexture`) with the same
   `setImage`/`CopyToHeap` recipe works, including sRGB textures and mipmaps,
   so compare the two: `FilamentUiRenderer::SyncTexture` is the only
   difference (dimensions 1024x512, re-uploads when the atlas version bumps).
   Filament's `libs/filagui/src/ImGuiHelper.cpp` is the closest known-good
   reference (it uploads ImGui's font atlas and samples it the same way).
   If this cannot be fixed quickly, draw text with the plain textured path
   from a colour atlas instead of the SDF channel.
2. **Skybox panoramas are ignored** (`GetSkyboxPath()`): the camera background
   colour is used instead. Needed for `examples/monkey_ball`.
3. **`RenderCamera::target_texture` cameras** (RenderTexture assets) are
   skipped with a warning, and `RenderMaterial::render_texture` is ignored.
4. **Area lights** are still parsed into `SceneLighting::area_lights` but not
   forwarded to Filament.
5. **`RenderSettings` / `PostProcessProfile` are ignored.** Views run with
   Filament defaults. Bloom, SSAO, exposure, tonemap mode, vignette, outline
   and FXAA settings do nothing.
6. **Black shadow blobs** appear on some surfaces (visible in the monkey ball
   track): tune `LightManager::ShadowOptions` (map size, cascades, bias) and
   the sun's shadow range for Mercury's scene scales.
7. **Engine teardown is skipped.** `~FilamentApplication` no longer calls
   `filament::Engine::destroy()`: on MoltenVK it panics (`texture used after it
   has been freed`) while retiring the last frame's descriptor sets, even after
   `flushAndWait()`. Everything is left to the OS at process exit. Find the
   dangling reference (likely the swapchain or a skybox destroyed just before)
   and restore the teardown.
8. **Editor readback stalls.** `Render()` calls `engine->flushAndWait()` every
   frame because readback callbacks must have fired. Ring-buffer the readback
   (or only flush when the editor asks for pixels) to get the frame rate back.

## Phase 3 goals

Work in small commits; every commit message must end with
`Assisted-By: gemini4:hydrogen`. Keep the total added code small — the whole
point of moving to Filament is that the library does the work.

### 1. Post-processing and camera quality

Map `RenderSettings` (see `mercury/scene/scene.h`) onto Filament `View` and
`ColorGrading`:

- `bloom_intensity`/`bloom_threshold`/`bloom_scatter` → `View::setBloomOptions`.
- `ssao_intensity`/`ssao_radius` → `View::setAmbientOcclusionOptions`
  (`AmbientOcclusionOptions::enabled/intensity/radius`), Filament uses GTAO.
- `tonemap`/`exposure` → `ColorGrading::Builder::toneMapper()` +
  `exposure()`, applied with `View::setColorGrading()`. Cache the
  `ColorGrading` object and rebuild only when values change.
- `vignette_*` → `View::setVignetteOptions`.
- `fxaa` → `View::setAntiAliasing(AntiAliasing::FXAA | NONE)`. Consider
  `TAA`/`MSAA` as the profile allows.
- `outline_layers`/`outline_color`/`outline_width`: Filament has no outline
  pass. Either drop the feature from the schema or draw the outline with a
  custom material (`materialDomain`, inverted hull) — measure before
  choosing.

Verify with `MERCURY_CAPTURE` screenshots of `examples/roll_a_ball` and
`examples/monkey_ball` before/after.

### 2. Image-based lighting from skybox panoramas

- Load the panorama (`ImageData`) and convert it to a cubemap with
  `libs/iblprefilter` (`IBLPrefilterContext`: `equirectangularToCubemap`,
  `specularFilter`, `irradiance`). Those libraries are already built and
  linked (`libfilament-iblprefilter.a`, `libibl.a`, `libimage.a`).
- Build a `filament::Skybox` from the cubemap and an `IndirectLight` with both
  reflections and irradiance; `Scene::setSkybox`/`setIndirectLight`.
- When no panorama is set, keep the current constant-SH ambient.
- Cache the cubemap per panorama path; destroy the old one when it changes
  (deferred, like the UI textures).

### 3. Materials: normal maps, emissive maps, transmission

Extend the lit materials (`.mat`) and `FilamentRenderer::BuildProxy`:

- `normal_texture` + `normal_scale` → a `normalMap` parameter and
  `material.normal = texture(...).xyz * 2 - 1` scaled by the strength. The
  geometry already uploads Filament-style TANGENTS quaternions.
- `emission_texture` → multiply `emissive`.
- `transmission`/`ior`/`thickness`/`transmission_color` →
  Filament's `refractionMode: screenspace`, `refractionType: solid|thin`,
  `transmission`, `ior`, `thickness`, `absorption` in a `lit_transmissive.mat`
  variant. This replaces hundreds of lines of the deleted refraction shader.

### 4. RenderTexture cameras

For each `RenderCamera` with a non-empty `target_texture`, create a
`filament::RenderTarget` (colour texture + depth) keyed by the resolved path
and size, render the view into it, and expose the colour texture to materials
that ask for it (`RenderMaterial::render_texture`). Filament keeps the texture
sampleable, so no copy is needed.

### 5. Area lights

Either approximate each `AreaLight` with a Filament `SPOT` light aimed along
the rectangle's normal (cheap, approximate), or use an emissive card plus the
existing IBL. Remove the struct from `components.h` only if you also migrate
maps; the map format already round-trips, and tests check it.

### 6. Performance and cleanup

- Editor readback: only `readPixels` for targets the editor actually asks for,
  and pipeline the callbacks (see gap 7).
- Delete what the Filament path no longer uses: the legacy `Mesh` /
  `MeshInstance` / `TextureAtlas` path in `mercury/pobject`,
  `mercury/properties/mesh.{h,cc}`, `mesh_instance.*`, `texture_atlas.*`,
  `MapScene::GetStaticVertices/GetStaticIndices`, and the corresponding
  `Application::OnNewTick` work. Note `scripts` and the editor read
  `GetTexture`/`GetModel`; check every caller before deleting.
- Sprite parity: `AttachSprite` now builds a `RenderMesh` (quad + unlit
  material) — verify `examples/pong`, `breakout` and `space_invaders` render
  and that sprite ordering (transparent, far-to-near) still looks right.

### 7. Tests

Add a headless render smoke test: start `FilamentApplication` headless with a
tiny scene, capture one frame, and assert the image is not uniform (a
non-blank render). Use `examples/cpp_hello` or a code-built scene. This is the
cheapest guard against a silently black renderer.

## Build and verification

```sh
# First build of Filament's CMake tree takes ~10 minutes; later builds cache.
bazelisk build //third_party:filament_build

bazelisk build //:mercury_player //:editor
bazelisk test //tests:mercury_test //tests:editor_test //tests:script_editor_test

# Visual checks (writes a PNG, then quits)
MERCURY_CAPTURE=/tmp/rab.png MERCURY_CAPTURE_FRAME=20 MERCURY_CAPTURE_QUIT=1 \
  ./bazel-bin/mercury/mercury_player examples/roll_a_ball/roll_a_ball.mproj
MERCURY_CAPTURE=/tmp/mb.png MERCURY_CAPTURE_FRAME=12 MERCURY_CAPTURE_QUIT=1 \
  ./bazel-bin/mercury/mercury_player examples/monkey_ball/monkey_ball.mproj
```

Notes:

- Filament panics (with a `PreconditionPanic`) rather than logging when a
  resource is misused: unbound samplers, a texture destroyed while a frame in
  flight still samples it, `generateMipmaps` without `GEN_MIPMAPPABLE`, etc.
  Keep the "retire for N frames" pattern used in `filament_ui.cc`.
- Matrices: Filament's `math::mat4` is column-major like glm, so pass glm's
  column order (`m[0][0], m[0][1], ...`). Getting this wrong silently flips or
  transposes the whole scene.
- Filament's cameras want a GL-convention projection (NDC z in [-1, 1]).
  Mercury builds its projections with `GLM_FORCE_DEPTH_ZERO_TO_ONE`, so check
  this if depth or culling looks wrong; `setProjection(fov, aspect, near,
  far, Fov::VERTICAL)` avoids the issue for perspective cameras.
- Material names may not contain spaces; the JSON-ish `.mat` parser rejects
  trailing commas and unknown keys (`version`, `format` on samplers).
- Sampler uniforms are read as `materialParams_<name>` in the fragment
  shader; material `variables` (custom attributes) are written in a `vertex`
  block from `getCustom0()` and read as `variable_<name>`.
