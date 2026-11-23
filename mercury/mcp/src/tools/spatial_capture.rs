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

/// Output resolution for `capture_viewport`.
///
/// An image costs `ceil(width / 28) * ceil(height / 28)` visual tokens
/// regardless of its format or color depth, so the resolution is the only
/// lever on cost. Each level sets the output's long edge; the image keeps the
/// aspect ratio of `width`/`height`, or 16:9 when neither is given.
#[derive(serde::Deserialize, schemars::JsonSchema)]
#[serde(rename_all = "snake_case")]
pub enum CaptureDetail {
    /// 672 px long edge: 672x378 (336 visual tokens) at 16:9.
    Low,
    /// 1008 px long edge: 1008x567 (756 visual tokens) at 16:9.
    Medium,
    /// 1568 px long edge: 1568x882 (1792 visual tokens) at 16:9.
    High,
    /// 2576 px long edge: 2576x1449 (4784 visual tokens) at 16:9.
    Max,
}

fn default_detail() -> CaptureDetail {
    CaptureDetail::Low
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
    /// Override `detail` with an explicit PNG width in pixels (64 to 2576).
    /// When only one of `width`/`height` is given, the other comes from 16:9.
    #[serde(default)]
    pub width: Option<u32>,
    /// Override `detail` with an explicit PNG height in pixels (64 to 2576).
    /// When only one of `width`/`height` is given, the other comes from 16:9.
    #[serde(default)]
    pub height: Option<u32>,
    /// Output resolution. Default `low` (672 px long edge, about 336 visual
    /// tokens at 16:9). `medium` is 1008 px, `high` 1568 px, `max` 2576 px.
    /// Explicit `width`/`height` override this.
    #[serde(default = "default_detail")]
    pub detail: CaptureDetail,
    /// Overlay a box around every collider node. Default false.
    #[serde(default)]
    pub colliders: bool,
    /// Overlay a box around each of these nodes.
    #[serde(default)]
    pub highlight: Vec<NodeSelector>,
    /// Render the scene view offscreen immediately instead of waiting for a
    /// presented frame. Useful while the editor window is minimized or hidden;
    /// default false. The result's `source` says which path produced the PNG.
    #[serde(default)]
    pub offscreen: bool,
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
        "Captures the 3D scene viewport as a PNG image. Returns a \
         low-resolution image by default (~336 visual tokens). Ask for \
         `detail: medium/high/max` only when you need to read small text or \
         fine detail; cost grows with pixel count (about width/28 times \
         height/28 tokens). `preset` is one of `current`, `top_down`, \
         `isometric`, `front` or `player_pov` (default `current`); `frame` \
         frames a node; explicit `width`/`height` (64 to 2576 px, long edge at \
         most 2576) override `detail`, deriving the other side from 16:9 when \
         only one is given. `colliders` overlays collider boxes and `highlight` \
         overlays boxes around the given nodes. No editor gizmos are drawn. \
         The result includes the output `width`/`height`, the `detail` used \
         (or `custom`), `visual_tokens`, the capture camera's `position`, \
         `target` and `fov`, and a `source` of `presented` (a freshly rendered \
         viewport frame) or `offscreen` (rendered on demand, e.g. while the \
         editor window is minimized). Set `offscreen: true` to skip the wait \
         for a presented frame entirely. The editor camera is always restored \
         after the capture, including on error. `player_pov` requires an \
         enabled Camera component. Fails with `no render host available` when \
         no render host is attached (e.g. a headless session without a \
         renderer).",
        Mode::Read,
        || schema::<CaptureViewportArgs>(),
    )
    .with_timeout_secs(60)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{Value, json};

    fn parse_args(value: Value) -> CaptureViewportArgs {
        serde_json::from_value(value).expect("capture_viewport args should parse")
    }

    #[test]
    fn detail_and_dimensions_default_to_low_resolution() {
        let args = parse_args(json!({}));
        assert!(matches!(args.detail, CaptureDetail::Low));
        assert!(args.width.is_none());
        assert!(args.height.is_none());
    }

    #[test]
    fn detail_accepts_every_level_and_rejects_unknown_ones() {
        let levels = [("low", 0), ("medium", 1), ("high", 2), ("max", 3)];
        for (name, expected) in levels {
            let args = parse_args(json!({ "detail": name }));
            let actual = match args.detail {
                CaptureDetail::Low => 0,
                CaptureDetail::Medium => 1,
                CaptureDetail::High => 2,
                CaptureDetail::Max => 3,
            };
            assert_eq!(actual, expected, "{name}");
        }
        assert!(
            serde_json::from_value::<CaptureViewportArgs>(json!({ "detail": "ultra" })).is_err()
        );
    }

    #[test]
    fn explicit_dimensions_parse_as_optional_overrides() {
        let args = parse_args(json!({ "width": 640, "height": 480 }));
        assert_eq!(args.width, Some(640));
        assert_eq!(args.height, Some(480));

        let schema = Value::Object(capture_viewport().schema());
        let width = &schema["properties"]["width"];
        assert!(width.get("description").is_some());
        assert!(!schema["properties"]["detail"]["description"].is_null());

        let detail_schema = serde_json::to_value(schemars::schema_for!(CaptureDetail))
            .expect("the detail schema should serialize");
        let rendered = detail_schema.to_string();
        for level in ["low", "medium", "high", "max"] {
            assert!(
                rendered.contains(&format!("\"{level}\"")),
                "{level} is missing from the schema: {rendered}"
            );
        }
    }

    #[test]
    fn the_description_states_the_token_cost_and_the_override() {
        let description = capture_viewport().description;
        assert!(description.contains("~336 visual tokens"), "{description}");
        assert!(description.contains("detail"), "{description}");
        assert!(description.contains("visual_tokens"), "{description}");
        assert!(description.contains("2576"), "{description}");
    }
}
