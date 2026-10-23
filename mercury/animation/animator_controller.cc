#include "animator_controller.h"

#include <format>
#include <map>

#include "os/filesystem.h"
#include "serialization/mtext.h"

namespace Mercury {

namespace {

float ToFloat(const mtext::Variant* value, float fallback = 0.0f) {
  if (!value) return fallback;
  if (auto b = value->ToBool(); b && value->GetType() == mtext::Variant::Type::kBool) {
    return *b ? 1.0f : 0.0f;
  }
  if (auto f = value->ToFloat()) return static_cast<float>(*f);
  return fallback;
}

}  // namespace

std::shared_ptr<AnimatorController> AnimatorController::Load(const std::string& file,
                                                             std::string* error) {
  auto text = os::ReadText(file);
  if (!text) {
    if (error) *error = "could not open " + file;
    return nullptr;
  }
  auto controller = FromString(*text, error);
  if (!controller && error) *error = file + ": " + *error;
  return controller;
}

std::shared_ptr<AnimatorController> AnimatorController::FromString(std::string_view text,
                                                                   std::string* error) {
  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::Parse(text, document, &parse_error)) {
    if (error) *error = parse_error.ToString();
    return nullptr;
  }
  auto controller = std::make_shared<AnimatorController>();
  std::map<std::string, int> layer_index;
  auto layer_of = [&](const mtext::Section& section) -> AnimatorLayer* {
    const std::string name = section.attributes.GetString("layer");
    auto it = layer_index.find(name);
    if (it == layer_index.end()) {
      // States before (or without) their [layer] get one.
      layer_index[name] = static_cast<int>(controller->layers_.size());
      controller->layers_.push_back(AnimatorLayer{name});
      return &controller->layers_.back();
    }
    return &controller->layers_[it->second];
  };

  for (const auto& section : document.sections) {
    const auto& a = section.attributes;
    const auto& p = section.properties;
    if (section.tag == "model") {
      if (auto model = p.Get("model")) {
        if (auto res = model->Get<mtext::Resource>()) controller->model_ = res->path;
        if (auto s = model->ToString()) controller->model_ = *s;
      }
      if (auto loops = p.Get("loop_clips")) {
        controller->loop_all_ = false;
        if (auto array = loops->Get<mtext::Array>()) {
          for (const auto& item : *array) {
            if (auto s = item.ToString()) controller->loop_clips_.insert(*s);
          }
        }
      }
      controller->root_motion_node_ = p.GetString("root_motion_node");
    } else if (section.tag == "parameter") {
      AnimatorParameter parameter;
      parameter.name = a.GetString("name");
      const std::string type = a.GetString("type", "float");
      if (type == "int") parameter.type = AnimatorParameter::Type::kInt;
      if (type == "bool") parameter.type = AnimatorParameter::Type::kBool;
      if (type == "trigger") parameter.type = AnimatorParameter::Type::kTrigger;
      parameter.default_value = ToFloat(a.Get("default"));
      controller->parameters_.push_back(parameter);
    } else if (section.tag == "layer") {
      const std::string name = a.GetString("name");
      if (!layer_index.contains(name)) {
        layer_index[name] = static_cast<int>(controller->layers_.size());
        controller->layers_.push_back(AnimatorLayer{name});
      }
      AnimatorLayer* layer = &controller->layers_[layer_index[name]];
      layer->weight = static_cast<float>(a.GetFloat("weight", 1.0));
      layer->additive = a.GetString("blending") == "additive";
    } else if (section.tag == "state") {
      AnimatorLayer* layer = layer_of(section);
      AnimatorState state;
      state.name = a.GetString("name");
      state.speed = static_cast<float>(a.GetFloat("speed", 1.0));
      state.speed_parameter = p.GetString("speed_parameter");
      state.clip = p.GetString("clip");
      const std::string blend = p.GetString("blend");
      if (blend == "1d") state.blend = AnimatorState::Blend::k1D;
      if (blend == "2d_simple") state.blend = AnimatorState::Blend::k2DSimple;
      if (blend == "2d_freeform") state.blend = AnimatorState::Blend::k2DFreeform;
      if (blend == "2d_cartesian") state.blend = AnimatorState::Blend::k2DCartesian;
      state.parameter = p.GetString("parameter");
      state.parameter_y = p.GetString("parameter_y");
      if (auto motions = p.Get("motions")) {
        if (auto array = motions->Get<mtext::Array>()) {
          for (const auto& item : *array) {
            auto parts = item.Get<mtext::Array>();
            if (!parts || parts->empty()) continue;
            BlendMotion motion;
            motion.clip = (*parts)[0].ToString().value_or("");
            if (parts->size() > 1) motion.threshold = ToFloat(&(*parts)[1]);
            if (parts->size() > 3) {
              motion.position = glm::vec2(ToFloat(&(*parts)[2]), ToFloat(&(*parts)[3]));
            }
            if (parts->size() > 4) motion.time_scale = ToFloat(&(*parts)[4], 1.0f);
            state.motions.push_back(motion);
          }
        }
      }
      if (a.GetBool("default", false)) layer->default_state = static_cast<int>(layer->states.size());
      layer->states.push_back(std::move(state));
    }
  }

  // Transitions name their states, so read them once every state exists.
  for (const auto& section : document.sections) {
    if (section.tag != "transition") continue;
    const auto& a = section.attributes;
    const auto& p = section.properties;
    AnimatorLayer* layer = layer_of(section);
    auto find = [&](const std::string& name) {
      for (size_t i = 0; i < layer->states.size(); i++) {
        if (layer->states[i].name == name) return static_cast<int>(i);
      }
      return -2;
    };
    AnimatorTransition transition;
    const std::string from = a.GetString("from");
    transition.from = from == "Any State" ? -1 : find(from);
    transition.to = find(a.GetString("to"));
    if (transition.from == -2 || transition.to < 0) {
      if (error) *error = std::format("{}: transition {} -> {} names a missing state",
                                      section.line, from, a.GetString("to"));
      return nullptr;
    }
    transition.duration = static_cast<float>(p.GetFloat("duration", 0.25));
    transition.fixed_duration = p.GetBool("fixed_duration", true);
    transition.offset = static_cast<float>(p.GetFloat("offset", 0.0));
    transition.has_exit_time = p.GetBool("has_exit_time", false);
    transition.exit_time = static_cast<float>(p.GetFloat("exit_time", 1.0));
    if (auto conditions = p.Get("conditions")) {
      if (auto array = conditions->Get<mtext::Array>()) {
        for (const auto& item : *array) {
          auto parts = item.Get<mtext::Array>();
          if (!parts || parts->size() < 2) continue;
          AnimatorCondition condition;
          condition.parameter = (*parts)[0].ToString().value_or("");
          const std::string mode = (*parts)[1].ToString().value_or("if");
          using Mode = AnimatorCondition::Mode;
          static const std::map<std::string, Mode> kModes = {
              {"if", Mode::kIf},           {"ifnot", Mode::kIfNot},
              {"greater", Mode::kGreater}, {"less", Mode::kLess},
              {"equals", Mode::kEquals},   {"notequal", Mode::kNotEqual}};
          if (auto it = kModes.find(mode); it != kModes.end()) condition.mode = it->second;
          if (parts->size() > 2) condition.threshold = ToFloat(&(*parts)[2]);
          transition.conditions.push_back(condition);
        }
      }
    }
    layer->transitions.push_back(std::move(transition));
  }
  return controller;
}

bool AnimatorController::IsLooping(std::string_view clip) const {
  return loop_all_ || loop_clips_.contains(clip);
}

}  // namespace Mercury
