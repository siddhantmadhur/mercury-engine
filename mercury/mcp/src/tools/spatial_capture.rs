// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The spatial-query and viewport-capture tool group.
//!
//! Both tools are Read mode and are implemented in C++ in
//! `editor/mcp/tools/spatial_capture.cc`. `query_scene_spatial` answers from
//! the edit-mode map's node components (there are no Jolt bodies in edit
//! mode); its raycasts intersect each node's *oriented bounding box*, the same
//! box editor picking uses. `capture_viewport` borrows the editor camera for
//! one capture, always restores it, and returns PNG bytes as an image.

use super::{Mode, NodeSelector, ToolSpec, schema};

/// A 3D vector as `[x, y, z]`.
pub type Vec3 = [f32; 3];

/// A world-space axis-aligned box.
#[allow(dead_code)]
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct BoxArg {
    /// Minimum corner `[x, y, z]`.
    pub min: Vec3,
    /// Maximum corner `[x, y, z]`.
    pub max: Vec3,
}

/// What `query_scene_spatial` should answer.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum SpatialQueryMode {
    /// World AABB and oriented bounds of one node (`node`).
    Bounds,
    /// Nodes whose world AABB overlaps `box`.
    NodesInBox,
    /// All oriented-bound hits along a ray, nearest first.
    Raycast,
    /// The same, casting straight down from `origin` by default.
    RaycastGround,
}

/// Arguments of `query_scene_spatial`.
#[allow(dead_code)]
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct QuerySceneSpatialArgs {
    /// The query to run.
    pub mode: SpatialQueryMode,
    /// The node to describe, for `bounds`.
    #[serde(default)]
    pub node: Option<NodeSelector>,
    /// The world-space box to test, for `nodes_in_box`.
    #[serde(default, rename = "box")]
    pub box_: Option<BoxArg>,
    /// Ray origin `[x, y, z]`, for `raycast` / `raycast_ground`.
    #[serde(default)]
    pub origin: Option<Vec3>,
    /// Ray direction, for `raycast` / `raycast_ground`. Defaults to straight
    /// down `[0, -1, 0]`; `raycast_ground` is the same query with that default.
    #[serde(default)]
    pub direction: Option<Vec3>,
    /// How far to follow the ray. Defaults to 10000.
    #[serde(default)]
    pub max_distance: Option<f32>,
    /// Bit N set tests nodes on layer N. Defaults to all layers.
    #[serde(default)]
    pub layer_mask: Option<u32>,
}

/// A camera preset for `capture_viewport`.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum CapturePreset {
    /// Leave the current editor view untouched.
    Current,
    /// Look straight down at the map (3D camera pitched to -89.9 degrees).
    TopDown,
    /// Three-quarter view.
    Isometric,
    /// Look along -Z from in front of the target.
    Front,
    /// The viewpoint of the map's player camera.
    PlayerPov,
}

fn default_preset() -> CapturePreset {
    CapturePreset::Current
}
fn default_width() -> u32 {
    1280
}
fn default_height() -> u32 {
    720
}

/// Arguments of `capture_viewport`.
#[allow(dead_code)]
#[derive(serde::Deserialize, schemars::JsonSchema)]
pub struct CaptureViewportArgs {
    /// Where to look. Default `current`. The camera is restored afterwards.
    #[serde(default = "default_preset")]
    pub preset: CapturePreset,
    /// Frame this node instead of using the preset's framing.
    #[serde(default)]
    pub frame: Option<NodeSelector>,
    /// PNG width in pixels (max 1920). Default 1280.
    #[serde(default = "default_width")]
    pub width: u32,
    /// PNG height in pixels (max 1080). Default 720.
    #[serde(default = "default_height")]
    pub height: u32,
    /// Overlay a box around every collider node. Default false.
    #[serde(default)]
    pub colliders: bool,
    /// Overlay a box around each of these nodes.
    #[serde(default)]
    pub highlight: Vec<NodeSelector>,
}

pub(crate) fn tools() -> Vec<ToolSpec> {
    vec![query_scene_spatial(), capture_viewport()]
}

fn query_scene_spatial() -> ToolSpec {
    ToolSpec::new(
        "query_scene_spatial",
        "Queries bounds and rays in the edit-mode scene (no Jolt physics \
         world is running). `bounds` returns a node's world AABB and its \
         oriented box; `nodes_in_box` finds nodes whose world AABB overlaps a \
         box; `raycast` and `raycast_ground` return oriented-bounding-box \
         hits, nearest first, with a world-space `normal` from the hit face, \
         honouring `layer_mask` and skipping inactive nodes. IMPORTANT: \
         edit-mode raycasts test each node's ORIENTED BOUNDING BOX (the same \
         box editor picking uses) with a slab test in the node's local space. \
         They do NOT test mesh triangles and do NOT use Jolt physics colliders.",
        Mode::Read,
        || schema::<QuerySceneSpatialArgs>(),
    )
}

fn capture_viewport() -> ToolSpec {
    ToolSpec::new(
        "capture_viewport",
        "Captures the 3D scene viewport as a PNG image. `preset` is one of \
         `current`, `top_down`, `isometric`, `front` or `player_pov` \
         (default `current`); `frame` frames a node; `width`/`height` default \
         to 1280x720 (max 1920x1080). `colliders` overlays collider boxes and \
         `highlight` overlays boxes around the given nodes. No editor gizmos \
         are drawn. The result includes the capture camera's `position`, \
         `target` and `fov`. The editor camera is always restored after the \
         capture, including on error. `player_pov` requires an enabled Camera \
         component. Fails with `no render host available` when no render host \
         is attached (e.g. a headless session without a renderer).",
        Mode::Read,
        || schema::<CaptureViewportArgs>(),
    )
    .with_timeout_secs(60)
}
