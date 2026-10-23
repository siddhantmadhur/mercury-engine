#include "animator.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

#include "math/mathf.h"

namespace Mercury {

AnimatorRuntime::AnimatorRuntime(std::shared_ptr<const AnimatorController> controller,
                                 std::shared_ptr<const Model> model)
    : controller_(std::move(controller)), model_(std::move(model)) {
  for (const auto& parameter : controller_->Parameters()) {
    parameters_[parameter.name] = {parameter.type, parameter.default_value, 0.0f};
  }
  for (const auto& layer : controller_->Layers()) {
    LayerState state;
    state.state = layer.default_state;
    layers_.push_back(state);
  }
  for (const auto& clip : model_->Clips()) clips_.emplace(clip.name, &clip);
  if (!controller_->GetRootMotionNode().empty()) {
    root_motion_node_ = model_->FindNode(controller_->GetRootMotionNode());
  }
  const size_t n = model_->Nodes().size();
  for (Pose* pose : {&layer_pose_, &final_pose_}) {
    pose->position.resize(n);
    pose->rotation.resize(n);
    pose->scale.resize(n);
    pose->weight.resize(n);
  }
}

// ------------------------------------------------------------ parameters --

void AnimatorRuntime::SetFloat(std::string_view name, float value) {
  auto it = parameters_.find(name);
  if (it != parameters_.end()) it->second.value = value;
}

void AnimatorRuntime::SetFloat(std::string_view name, float value, float damp_time,
                               float delta_time) {
  auto it = parameters_.find(name);
  if (it == parameters_.end()) return;
  if (damp_time <= 0.0f || delta_time <= 0.0f) {
    it->second.value = value;
    return;
  }
  it->second.value = Mathf::SmoothDamp(it->second.value, value, it->second.velocity,
                                       damp_time, delta_time);
}

void AnimatorRuntime::SetInteger(std::string_view name, int value) {
  SetFloat(name, static_cast<float>(value));
}

void AnimatorRuntime::SetBool(std::string_view name, bool value) {
  SetFloat(name, value ? 1.0f : 0.0f);
}

void AnimatorRuntime::SetTrigger(std::string_view name) { SetFloat(name, 1.0f); }
void AnimatorRuntime::ResetTrigger(std::string_view name) { SetFloat(name, 0.0f); }

float AnimatorRuntime::GetFloat(std::string_view name) const {
  auto it = parameters_.find(name);
  return it != parameters_.end() ? it->second.value : 0.0f;
}

int AnimatorRuntime::GetInteger(std::string_view name) const {
  return static_cast<int>(std::lround(GetFloat(name)));
}

bool AnimatorRuntime::GetBool(std::string_view name) const { return GetFloat(name) != 0.0f; }

bool AnimatorRuntime::HasParameter(std::string_view name) const {
  return parameters_.contains(name);
}

// ----------------------------------------------------------------- states --

void AnimatorRuntime::Play(std::string_view state, int layer, float normalized_time) {
  const auto& layers = controller_->Layers();
  for (size_t l = 0; l < layers.size(); l++) {
    if (layer >= 0 && static_cast<int>(l) != layer) continue;
    for (size_t s = 0; s < layers[l].states.size(); s++) {
      if (layers[l].states[s].name != state) continue;
      layers_[l] = LayerState{static_cast<int>(s), normalized_time};
      return;
    }
  }
}

std::string AnimatorRuntime::GetCurrentState(int layer) const {
  if (layer < 0 || layer >= static_cast<int>(layers_.size())) return "";
  const auto& states = controller_->Layers()[layer].states;
  const int index = layers_[layer].state;
  return index >= 0 && index < static_cast<int>(states.size()) ? states[index].name : "";
}

float AnimatorRuntime::GetNormalizedTime(int layer) const {
  return layer >= 0 && layer < static_cast<int>(layers_.size()) ? layers_[layer].time : 0.0f;
}

bool AnimatorRuntime::IsInTransition(int layer) const {
  return layer >= 0 && layer < static_cast<int>(layers_.size()) && layers_[layer].next >= 0;
}

const AnimationClip* AnimatorRuntime::Clip(std::string_view name) const {
  if (name.empty()) return nullptr;
  auto it = clips_.find(name);
  if (it != clips_.end()) return it->second;
  return model_->FindClip(name);
}

float AnimatorRuntime::Speed(const AnimatorState& state) const {
  float speed = state.speed;
  if (!state.speed_parameter.empty()) speed *= GetFloat(state.speed_parameter);
  return speed;
}

void AnimatorRuntime::StateWeights(const AnimatorState& state,
                                   std::vector<float>& weights) const {
  const size_t n = state.motions.size();
  weights.assign(n, 0.0f);
  if (n == 0) return;
  if (n == 1) {
    weights[0] = 1.0f;
    return;
  }
  if (state.blend == AnimatorState::Blend::k1D) {
    const float p = GetFloat(state.parameter);
    // Thresholds ascend in Unity's trees; clamp outside the range.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return state.motions[a].threshold < state.motions[b].threshold;
    });
    if (p <= state.motions[order.front()].threshold) {
      weights[order.front()] = 1.0f;
      return;
    }
    if (p >= state.motions[order.back()].threshold) {
      weights[order.back()] = 1.0f;
      return;
    }
    for (size_t i = 0; i + 1 < n; i++) {
      const float a = state.motions[order[i]].threshold;
      const float b = state.motions[order[i + 1]].threshold;
      if (p >= a && p <= b) {
        const float t = b > a ? (p - a) / (b - a) : 0.0f;
        weights[order[i]] = 1.0f - t;
        weights[order[i + 1]] = t;
        return;
      }
    }
    return;
  }
  // 2D: gradient band interpolation (Johansen, "Automated Semi-Procedural
  // Animation"), the method behind Unity's freeform trees.
  const glm::vec2 p(GetFloat(state.parameter), GetFloat(state.parameter_y));
  float total = 0.0f;
  for (size_t i = 0; i < n; i++) {
    const glm::vec2 pi = state.motions[i].position;
    if (glm::length(p - pi) < 1e-5f) {
      weights.assign(n, 0.0f);
      weights[i] = 1.0f;
      return;
    }
    float weight = 1.0f;
    for (size_t j = 0; j < n; j++) {
      if (j == i) continue;
      const glm::vec2 pj = state.motions[j].position;
      const glm::vec2 pij = pj - pi;
      const float length2 = glm::dot(pij, pij);
      if (length2 < 1e-8f) continue;
      const float h = 1.0f - glm::dot(p - pi, pij) / length2;
      weight = std::min(weight, h);
    }
    weights[i] = std::max(weight, 0.0f);
    total += weights[i];
  }
  if (total > 0.0f) {
    for (float& w : weights) w /= total;
  } else {
    // Outside every band: the nearest motion.
    size_t nearest = 0;
    for (size_t i = 1; i < n; i++) {
      if (glm::length(p - state.motions[i].position) <
          glm::length(p - state.motions[nearest].position)) {
        nearest = i;
      }
    }
    weights[nearest] = 1.0f;
  }
}

float AnimatorRuntime::StateLength(const AnimatorState& state) const {
  if (state.blend == AnimatorState::Blend::kClip) {
    const AnimationClip* clip = Clip(state.clip);
    return clip && clip->duration > 0.0f ? clip->duration : 1.0f;
  }
  // A blend tree's length is its motions' lengths weighted like the pose.
  std::vector<float> weights;
  StateWeights(state, weights);
  float length = 0.0f;
  float total = 0.0f;
  for (size_t i = 0; i < state.motions.size(); i++) {
    const AnimationClip* clip = Clip(state.motions[i].clip);
    if (!clip || weights[i] <= 0.0f) continue;
    const float scale = std::max(std::abs(state.motions[i].time_scale), 1e-3f);
    length += weights[i] * clip->duration / scale;
    total += weights[i];
  }
  return total > 0.0f && length > 0.0f ? length / total : 1.0f;
}

void AnimatorRuntime::ResetPose(Pose& pose) const {
  std::fill(pose.weight.begin(), pose.weight.end(), 0.0f);
  std::fill(pose.position.begin(), pose.position.end(), glm::vec3(0.0f));
  std::fill(pose.rotation.begin(), pose.rotation.end(), glm::quat(0.0f, 0.0f, 0.0f, 0.0f));
  std::fill(pose.scale.begin(), pose.scale.end(), glm::vec3(0.0f));
}

void AnimatorRuntime::SampleClip(const AnimationClip& clip, float seconds, float weight,
                                 Pose& pose) const {
  const auto& nodes = model_->Nodes();
  for (const auto& channel : clip.channels) {
    if (channel.node < 0 || channel.node >= static_cast<int>(nodes.size())) continue;
    const ModelNode& rest = nodes[channel.node];
    glm::vec3 position = rest.position;
    glm::quat rotation = rest.rotation;
    glm::vec3 scale = rest.scale;
    clip.Sample(channel, seconds, position, rotation, scale);
    const int n = channel.node;
    pose.position[n] += position * weight;
    // Keep quaternions in one hemisphere before averaging.
    if (glm::dot(pose.rotation[n], rotation) < 0.0f) rotation = -rotation;
    pose.rotation[n] += rotation * weight;
    pose.scale[n] += scale * weight;
    pose.weight[n] += weight;
  }
}

void AnimatorRuntime::SampleState(const AnimatorState& state, float normalized_time,
                                  float weight, Pose& pose) const {
  auto sample = [&](const std::string& name, float time_scale, float w) {
    const AnimationClip* clip = Clip(name);
    if (!clip || w <= 0.0f) return;
    const float duration = std::max(clip->duration, 1e-4f);
    float t = normalized_time * time_scale;
    if (controller_->IsLooping(name)) {
      t -= std::floor(t);
    } else {
      t = std::clamp(t, 0.0f, 1.0f);
    }
    SampleClip(*clip, t * duration, w, pose);
  };
  if (state.blend == AnimatorState::Blend::kClip) {
    sample(state.clip, 1.0f, weight);
    return;
  }
  std::vector<float> weights;
  StateWeights(state, weights);
  for (size_t i = 0; i < state.motions.size(); i++) {
    sample(state.motions[i].clip, 1.0f, weights[i] * weight);
  }
}

bool AnimatorRuntime::ConditionsMet(const AnimatorTransition& transition) const {
  for (const auto& condition : transition.conditions) {
    const float value = GetFloat(condition.parameter);
    using Mode = AnimatorCondition::Mode;
    switch (condition.mode) {
      case Mode::kIf: if (value == 0.0f) return false; break;
      case Mode::kIfNot: if (value != 0.0f) return false; break;
      case Mode::kGreater: if (!(value > condition.threshold)) return false; break;
      case Mode::kLess: if (!(value < condition.threshold)) return false; break;
      case Mode::kEquals: if (value != condition.threshold) return false; break;
      case Mode::kNotEqual: if (value == condition.threshold) return false; break;
    }
  }
  return true;
}

void AnimatorRuntime::ConsumeTriggers(const AnimatorTransition& transition) {
  for (const auto& condition : transition.conditions) {
    auto it = parameters_.find(condition.parameter);
    if (it != parameters_.end() && it->second.type == AnimatorParameter::Type::kTrigger) {
      it->second.value = 0.0f;
    }
  }
}

void AnimatorRuntime::Update(float dt, std::vector<glm::mat4>& locals) {
  const auto& nodes = model_->Nodes();
  const auto& layers = controller_->Layers();
  ResetPose(final_pose_);
  bool any = false;

  for (size_t l = 0; l < layers.size(); l++) {
    const AnimatorLayer& layer = layers[l];
    if (layer.states.empty()) continue;
    LayerState& run = layers_[l];
    run.state = std::clamp(run.state, 0, static_cast<int>(layer.states.size()) - 1);
    const AnimatorState& current = layer.states[run.state];

    // Advance time.
    const float length = StateLength(current);
    run.time += dt * Speed(current) / std::max(length, 1e-4f);
    if (run.next >= 0) {
      const AnimatorState& next = layer.states[run.next];
      run.next_time += dt * Speed(next) / std::max(StateLength(next), 1e-4f);
      run.blend += run.blend_duration > 0.0f ? dt / run.blend_duration : 1.0f;
      if (run.blend >= 1.0f) {
        run.state = run.next;
        run.time = run.next_time;
        run.next = -1;
        run.blend = 0.0f;
      }
    }

    // Start a transition (not while one is running).
    if (run.next < 0) {
      for (const auto& transition : layer.transitions) {
        if (transition.from != -1 && transition.from != run.state) continue;
        if (transition.from == -1 && transition.to == run.state) continue;
        // Exit time: how far (normalized) the state must have played.
        if (transition.has_exit_time && run.time < transition.exit_time) continue;
        if (!ConditionsMet(transition)) continue;
        if (transition.conditions.empty() && !transition.has_exit_time) continue;
        ConsumeTriggers(transition);
        const float state_length = StateLength(layer.states[run.state]);
        run.next = transition.to;
        run.next_time = transition.offset;
        run.blend = 0.0f;
        run.blend_duration = transition.fixed_duration ? transition.duration
                                                       : transition.duration * state_length;
        if (run.blend_duration <= 0.0f) {
          run.state = run.next;
          run.time = run.next_time;
          run.next = -1;
        }
        break;
      }
    }

    // Sample this layer.
    ResetPose(layer_pose_);
    const float blend = run.next >= 0 ? std::clamp(run.blend, 0.0f, 1.0f) : 0.0f;
    SampleState(layer.states[run.state], run.time, 1.0f - blend, layer_pose_);
    if (run.next >= 0) SampleState(layer.states[run.next], run.next_time, blend, layer_pose_);

    // Override blending: the layer replaces what is below by its weight.
    const float layer_weight = l == 0 ? 1.0f : layer.weight;
    for (size_t n = 0; n < nodes.size(); n++) {
      const float w = layer_pose_.weight[n];
      if (w <= 1e-6f) continue;
      const glm::vec3 position = layer_pose_.position[n] / w;
      const glm::quat rotation = glm::normalize(layer_pose_.rotation[n]);
      const glm::vec3 scale = layer_pose_.scale[n] / w;
      if (final_pose_.weight[n] <= 0.0f || layer_weight >= 1.0f) {
        final_pose_.position[n] = position;
        final_pose_.rotation[n] = rotation;
        final_pose_.scale[n] = scale;
      } else {
        final_pose_.position[n] = glm::mix(final_pose_.position[n], position, layer_weight);
        final_pose_.rotation[n] = glm::slerp(final_pose_.rotation[n], rotation, layer_weight);
        final_pose_.scale[n] = glm::mix(final_pose_.scale[n], scale, layer_weight);
      }
      final_pose_.weight[n] = 1.0f;
      any = true;
    }
  }

  // Triggers that no transition used stay set, as in Unity.
  locals.resize(nodes.size());
  for (size_t n = 0; n < nodes.size(); n++) {
    const ModelNode& rest = nodes[n];
    glm::vec3 position = rest.position;
    glm::quat rotation = rest.rotation;
    glm::vec3 scale = rest.scale;
    if (any && final_pose_.weight[n] > 0.0f) {
      position = final_pose_.position[n];
      rotation = final_pose_.rotation[n];
      scale = final_pose_.scale[n];
    }
    // Without root motion the character stays where its object is: the
    // root motion node keeps its rest position on the ground plane (model
    // space X and Z), only moving up and down.
    if (hold_root_ && static_cast<int>(n) == root_motion_node_ && rest.parent >= 0) {
      const glm::mat3 parent = glm::mat3(model_->BindGlobals()[rest.parent]);
      glm::vec3 delta = parent * (position - rest.position);
      delta.x = 0.0f;
      delta.z = 0.0f;
      position = rest.position + glm::inverse(parent) * delta;
    }
    locals[n] = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.0f), scale);
  }
}

}  // namespace Mercury
