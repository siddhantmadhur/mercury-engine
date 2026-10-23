/*
 * animator.h
 *
 * Runs an AnimatorController on a model: parameters, the current state of
 * each layer, crossfades, and the blended pose (Unity's Animator).
 */
#ifndef MERCURY_ANIMATION_ANIMATOR_H
#define MERCURY_ANIMATION_ANIMATOR_H

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "animation/animator_controller.h"
#include "properties/model.h"

namespace Mercury {

class AnimatorRuntime {
 public:
  AnimatorRuntime(std::shared_ptr<const AnimatorController> controller,
                  std::shared_ptr<const Model> model);

  void SetFloat(std::string_view name, float value);
  // Moves towards `value` over about `damp_time` seconds (Unity's damped
  // SetFloat).
  void SetFloat(std::string_view name, float value, float damp_time, float delta_time);
  void SetInteger(std::string_view name, int value);
  void SetBool(std::string_view name, bool value);
  void SetTrigger(std::string_view name);
  void ResetTrigger(std::string_view name);
  float GetFloat(std::string_view name) const;
  int GetInteger(std::string_view name) const;
  bool GetBool(std::string_view name) const;
  bool HasParameter(std::string_view name) const;

  // Jumps a layer straight to a state (Animator.Play).
  void Play(std::string_view state, int layer = -1, float normalized_time = 0.0f);
  // Name of the layer's current state.
  std::string GetCurrentState(int layer) const;
  float GetNormalizedTime(int layer) const;
  bool IsInTransition(int layer) const;

  // Hold the root motion node in place (applyRootMotion = false).
  void SetHoldRoot(bool hold) { hold_root_ = hold; }

  /**
   * @brief Advance by dt seconds and write node local matrices
   * @param locals One per model node; starts from the model's rest pose
   */
  void Update(float dt, std::vector<glm::mat4>& locals);

  const Model& GetModel() const { return *model_; }

 private:
  struct Pose {
    std::vector<glm::vec3> position;
    std::vector<glm::quat> rotation;
    std::vector<glm::vec3> scale;
    std::vector<float> weight;  // per node: how much of it was written
  };
  struct LayerState {
    int state = 0;
    float time = 0.0f;  // normalized, keeps counting past 1
    int next = -1;
    float next_time = 0.0f;
    float blend = 0.0f;  // 0..1 through the transition
    float blend_duration = 0.0f;  // seconds
  };
  struct ParameterValue {
    AnimatorParameter::Type type;
    float value = 0.0f;
    float velocity = 0.0f;  // damped SetFloat
  };

  const AnimationClip* Clip(std::string_view name) const;
  // Weighted motions of a state for the current parameters.
  void StateWeights(const AnimatorState& state, std::vector<float>& weights) const;
  float StateLength(const AnimatorState& state) const;  // seconds
  void SampleState(const AnimatorState& state, float normalized_time, float weight,
                   Pose& pose) const;
  void SampleClip(const AnimationClip& clip, float seconds, float weight, Pose& pose) const;
  bool ConditionsMet(const AnimatorTransition& transition) const;
  void ConsumeTriggers(const AnimatorTransition& transition);
  float Speed(const AnimatorState& state) const;
  void ResetPose(Pose& pose) const;

  std::shared_ptr<const AnimatorController> controller_;
  std::shared_ptr<const Model> model_;
  std::map<std::string, ParameterValue, std::less<>> parameters_;
  std::vector<LayerState> layers_;
  std::map<std::string, const AnimationClip*, std::less<>> clips_;
  std::map<const AnimationClip*, std::vector<int>> channel_nodes_;
  int root_motion_node_ = -1;
  bool hold_root_ = true;
  Pose layer_pose_;
  Pose final_pose_;
};

}  // namespace Mercury

#endif  // MERCURY_ANIMATION_ANIMATOR_H
