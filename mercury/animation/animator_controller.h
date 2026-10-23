/*
 * animator_controller.h
 *
 * A state machine that picks and blends a model's animation clips from
 * parameters set by scripts (Unity's Animator Controller). Stored in a
 * .manim file:
 *
 *   [manimator format=1]
 *
 *   [model]
 *   model = Resource("res://characters/hero.fbx")
 *   loop_clips = ["Armature|Run", "Armature|Idle"]
 *   root_motion_node = "root"
 *
 *   [parameter name="Speed" type="float" default=0.0]
 *   [parameter name="Jump" type="bool" default=false]
 *
 *   [layer name="Base" weight=1.0 blending="override"]
 *
 *   [state layer="Base" name="Move" speed=1.0 default=true]
 *   blend = "2d_freeform"            ; or "1d", "2d_simple", ...; or
 *   parameter = "VelocityX"          ; clip = "Armature|Idle" for one clip
 *   parameter_y = "VelocityY"
 *   motions = [["Armature|Idle", 0.0, 0.0, 0.0, 1.0], ...]
 *             ; clip, threshold (1d), x, y (2d), time scale
 *
 *   [transition layer="Base" from="Move" to="Jump"]   ; from "Any State"
 *   duration = 0.25
 *   fixed_duration = true            ; seconds (false: fraction of the state)
 *   has_exit_time = false
 *   exit_time = 0.9
 *   conditions = [["Jump", "if", 0.0]]   ; if, ifnot, greater, less,
 *                                        ; equals, notequal
 */
#ifndef MERCURY_ANIMATION_ANIMATOR_CONTROLLER_H
#define MERCURY_ANIMATION_ANIMATOR_CONTROLLER_H

#include <glm/glm.hpp>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Mercury {

struct AnimatorParameter {
  enum class Type { kFloat, kInt, kBool, kTrigger };
  std::string name;
  Type type = Type::kFloat;
  float default_value = 0.0f;
};

struct BlendMotion {
  std::string clip;
  float threshold = 0.0f;
  glm::vec2 position = glm::vec2(0.0f);
  float time_scale = 1.0f;
};

struct AnimatorState {
  enum class Blend { kClip, k1D, k2DSimple, k2DFreeform, k2DCartesian };
  std::string name;
  float speed = 1.0f;
  std::string speed_parameter;
  Blend blend = Blend::kClip;
  std::string clip;
  std::string parameter;
  std::string parameter_y;
  std::vector<BlendMotion> motions;
};

struct AnimatorCondition {
  enum class Mode { kIf, kIfNot, kGreater, kLess, kEquals, kNotEqual };
  std::string parameter;
  Mode mode = Mode::kIf;
  float threshold = 0.0f;
};

struct AnimatorTransition {
  int from = -1;  // -1: Any State
  int to = 0;
  float duration = 0.25f;
  bool fixed_duration = true;
  float offset = 0.0f;
  bool has_exit_time = false;
  float exit_time = 1.0f;
  std::vector<AnimatorCondition> conditions;
};

struct AnimatorLayer {
  std::string name;
  float weight = 1.0f;
  bool additive = false;
  std::vector<AnimatorState> states;
  int default_state = 0;
  std::vector<AnimatorTransition> transitions;
};

class AnimatorController {
 public:
  static std::shared_ptr<AnimatorController> Load(const std::string& file,
                                                  std::string* error = nullptr);
  static std::shared_ptr<AnimatorController> FromString(std::string_view text,
                                                        std::string* error = nullptr);

  const std::vector<AnimatorParameter>& Parameters() const { return parameters_; }
  const std::vector<AnimatorLayer>& Layers() const { return layers_; }
  // The model whose clips the states name (res:// path), may be empty.
  const std::string& GetModel() const { return model_; }
  bool IsLooping(std::string_view clip) const;
  const std::string& GetRootMotionNode() const { return root_motion_node_; }

 private:
  std::vector<AnimatorParameter> parameters_;
  std::vector<AnimatorLayer> layers_;
  std::string model_;
  std::set<std::string, std::less<>> loop_clips_;
  bool loop_all_ = true;  // no loop list given: every clip loops
  std::string root_motion_node_;
};

}  // namespace Mercury

#endif  // MERCURY_ANIMATION_ANIMATOR_CONTROLLER_H
