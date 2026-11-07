/*
 * bindings_components.cc
 *
 * Generic access to the engine's built-in components. The TypeScript classes in
 * generated/components.ts are made from the TypeRegistry, and each typed property calls these
 * with the component and field name.
 */
#include "scripting/js/bindings.h"
#include "animation/animator.h"
#include "audio/audio_system.h"
#include "components/components.h"
#include "scripting/type_registry.h"

namespace Mercury::js {

namespace {

v8::Local<v8::Value> VariantToJs(v8::Isolate* isolate, const mtext::Variant& value) {
  using T = mtext::Variant::Type;
  auto number_array = [&](std::initializer_list<double> numbers) -> v8::Local<v8::Value> {
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Array> array = v8::Array::New(isolate, static_cast<int>(numbers.size()));
    uint32_t i = 0;
    for (double n : numbers) array->Set(context, i++, v8::Number::New(isolate, n)).Check();
    return array;
  };
  switch (value.GetType()) {
    case T::kBool: return v8::Boolean::New(isolate, value.ToBool().value_or(false));
    case T::kInt: return v8::Number::New(isolate, static_cast<double>(value.ToInt().value_or(0)));
    case T::kFloat: return v8::Number::New(isolate, value.ToFloat().value_or(0.0));
    case T::kString: return Str(isolate, value.ToString().value_or(""));
    case T::kVector2: {
      auto v = *value.Get<glm::vec2>();
      return number_array({v.x, v.y});
    }
    case T::kVector3: {
      auto v = *value.Get<glm::vec3>();
      return number_array({v.x, v.y, v.z});
    }
    case T::kColor: {
      auto v = value.Get<mtext::Color>()->rgba;
      return number_array({v.r, v.g, v.b, v.a});
    }
    case T::kNodePath: return Str(isolate, value.Get<mtext::NodePath>()->path);
    case T::kResource: return Str(isolate, value.Get<mtext::Resource>()->path);
    case T::kArray: {
      v8::Local<v8::Context> context = isolate->GetCurrentContext();
      const auto& items = *value.Get<mtext::Array>();
      v8::Local<v8::Array> array = v8::Array::New(isolate, static_cast<int>(items.size()));
      for (uint32_t i = 0; i < items.size(); i++) {
        array->Set(context, i, VariantToJs(isolate, items[i])).Check();
      }
      return array;
    }
    default: return v8::Undefined(isolate);
  }
}

bool NumbersOf(v8::Local<v8::Context> context, v8::Local<v8::Value> value, double* out, uint32_t count) {
  if (!value->IsArray()) return false;
  v8::Local<v8::Array> array = value.As<v8::Array>();
  if (array->Length() < count) return false;
  for (uint32_t i = 0; i < count; i++) {
    v8::Local<v8::Value> item;
    if (!array->Get(context, i).ToLocal(&item) || !item->IsNumber()) return false;
    out[i] = item.As<v8::Number>()->Value();
  }
  return true;
}

std::optional<mtext::Variant> JsToVariant(v8::Isolate* isolate, v8::Local<v8::Value> value,
                                         FieldType type) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  double n[4];
  switch (type) {
    case FieldType::kBool:
      if (value->IsBoolean()) return mtext::Variant(value.As<v8::Boolean>()->Value());
      break;
    case FieldType::kInt:
      if (value->IsNumber()) return mtext::Variant(static_cast<int>(value.As<v8::Number>()->Value()));
      break;
    case FieldType::kFloat:
      if (value->IsNumber()) return mtext::Variant(value.As<v8::Number>()->Value());
      break;
    case FieldType::kString:
    case FieldType::kEnum:
      if (value->IsString()) {
        v8::String::Utf8Value utf8(isolate, value);
        return mtext::Variant(std::string(*utf8, static_cast<size_t>(utf8.length())));
      }
      break;
    case FieldType::kVector2:
      if (NumbersOf(context, value, n, 2)) return mtext::Variant(glm::vec2(n[0], n[1]));
      break;
    case FieldType::kVector3:
      if (NumbersOf(context, value, n, 3)) return mtext::Variant(glm::vec3(n[0], n[1], n[2]));
      break;
    case FieldType::kColor:
      if (NumbersOf(context, value, n, 4)) {
        return mtext::Variant(mtext::Color{glm::vec4(n[0], n[1], n[2], n[3])});
      }
      break;
    case FieldType::kNodeRef:
      if (value->IsString()) {
        v8::String::Utf8Value utf8(isolate, value);
        return mtext::Variant(mtext::NodePath{std::string(*utf8, static_cast<size_t>(utf8.length()))});
      }
      break;
    case FieldType::kAsset:
      if (value->IsString()) {
        v8::String::Utf8Value utf8(isolate, value);
        return mtext::Variant(mtext::Resource{std::string(*utf8, static_cast<size_t>(utf8.length()))});
      }
      break;
    default: break;
  }
  return std::nullopt;
}

const ComponentInfo* ArgComponentType(const Info& info, int i) {
  std::string type;
  if (!ArgString(info, i, &type)) return nullptr;
  const ComponentInfo* component = TypeRegistry::Instance().FindComponent(type);
  if (!component) Throw(info.GetIsolate(), "Unknown component type " + type);
  return component;
}

void ComponentHas(const Info& info) {
  auto object = ArgGameObject(info, 0);
  const ComponentInfo* component = ArgComponentType(info, 2);
  if (!object || !component) return;
  info.GetReturnValue().Set(component->has && component->has(*object->GetScene(), object->GetEntity()));
}

void ComponentGet(const Info& info) {
  auto object = ArgGameObject(info, 0);
  const ComponentInfo* component = ArgComponentType(info, 2);
  std::string field;
  if (!object || !component || !ArgString(info, 3, &field)) return;
  mtext::Variant value;
  if (!component->get_field ||
      !component->get_field(*object->GetScene(), object->GetEntity(), field, &value)) {
    Throw(info.GetIsolate(), "The object has no " + component->name + " component with a field " + field);
    return;
  }
  info.GetReturnValue().Set(VariantToJs(info.GetIsolate(), value));
}

void ComponentSet(const Info& info) {
  auto object = ArgGameObject(info, 0);
  const ComponentInfo* component = ArgComponentType(info, 2);
  std::string field;
  if (!object || !component || !ArgString(info, 3, &field) || info.Length() < 5) return;
  const FieldInfo* field_info = component->FindField(field);
  if (!field_info) {
    Throw(info.GetIsolate(), component->name + " has no field " + field);
    return;
  }
  if (!field_info->runtime_writable) {
    Throw(info.GetIsolate(), component->name + "." + field + " cannot be changed at run time");
    return;
  }
  auto value = JsToVariant(info.GetIsolate(), info[4], field_info->type);
  if (!value) {
    ThrowType(info.GetIsolate(), "Wrong type for " + component->name + "." + field);
    return;
  }
  if (!component->set_field || !component->set_field(*object->GetScene(), object->GetEntity(), field, *value)) {
    Throw(info.GetIsolate(), "The object has no " + component->name + " component");
  }
}

void ComponentAdd(const Info& info) {
  auto object = ArgGameObject(info, 0);
  const ComponentInfo* component = ArgComponentType(info, 2);
  if (!object || !component || info.Length() < 4 || !info[3]->IsObject()) return;
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  mtext::PropertyList properties;
  v8::Local<v8::Object> props = info[3].As<v8::Object>();
  v8::Local<v8::Array> names;
  if (props->GetOwnPropertyNames(context).ToLocal(&names)) {
    for (uint32_t i = 0; i < names->Length(); i++) {
      v8::Local<v8::Value> key, value;
      if (!names->Get(context, i).ToLocal(&key) || !props->Get(context, key).ToLocal(&value)) continue;
      v8::String::Utf8Value name(isolate, key);
      const FieldInfo* field = component->FindField(std::string(*name, static_cast<size_t>(name.length())));
      if (!field) {
        Throw(isolate, component->name + " has no field " + std::string(*name));
        return;
      }
      auto variant = JsToVariant(isolate, value, field->type);
      if (!variant) {
        ThrowType(isolate, "Wrong type for " + component->name + "." + field->name);
        return;
      }
      properties.Set(field->name, std::move(*variant));
    }
  }
  if (!object->GetScene()->AddComponent(*object, component->name, properties)) {
    Throw(isolate, "Could not add " + component->name);
  }
}

// --- runtime methods of Animator, AudioSource and CharacterController ----------------------------

template <typename T>
T* ArgComponent(const Info& info, int i = 0) {
  auto object = ArgGameObject(info, i);
  if (!object) return nullptr;
  T* component = object->GetComponent<T>();
  if (!component) Throw(info.GetIsolate(), "The object has no such component");
  return component;
}

// animatorSet(scene, entity, kind, name, value): kind 0 float, 1 integer, 2 bool, 3 trigger,
// 4 reset trigger
void AnimatorSet(const Info& info) {
  auto* animator = ArgComponent<Animator>(info);
  int kind;
  std::string name;
  if (!animator || !ArgInt(info, 2, &kind) || !ArgString(info, 3, &name)) return;
  double number = 0;
  bool flag = false;
  if (kind <= 1 && !ArgNumber(info, 4, &number)) return;
  if (kind == 2 && !ArgBool(info, 4, &flag)) return;
  switch (kind) {
    case 0: animator->SetFloat(name, static_cast<float>(number)); break;
    case 1: animator->SetInteger(name, static_cast<int>(number)); break;
    case 2: animator->SetBool(name, flag); break;
    case 3: animator->SetTrigger(name); break;
    default: animator->ResetTrigger(name); break;
  }
}

// animatorGet(scene, entity, kind, name): kind 0 float, 1 integer, 2 bool
void AnimatorGet(const Info& info) {
  auto* animator = ArgComponent<Animator>(info);
  int kind;
  std::string name;
  if (!animator || !ArgInt(info, 2, &kind) || !ArgString(info, 3, &name)) return;
  if (kind == 0) info.GetReturnValue().Set(static_cast<double>(animator->GetFloat(name)));
  else if (kind == 1) info.GetReturnValue().Set(static_cast<double>(animator->GetInteger(name)));
  else info.GetReturnValue().Set(animator->GetBool(name));
}

void AnimatorPlay(const Info& info) {
  auto* animator = ArgComponent<Animator>(info);
  std::string state;
  int layer;
  double time;
  if (!animator || !ArgString(info, 2, &state) || !ArgInt(info, 3, &layer) ||
      !ArgNumber(info, 4, &time)) {
    return;
  }
  animator->Play(state, layer, static_cast<float>(time));
}

void AnimatorState(const Info& info) {
  auto* animator = ArgComponent<Animator>(info);
  int layer;
  if (animator && ArgInt(info, 2, &layer)) {
    info.GetReturnValue().Set(Str(info.GetIsolate(), animator->GetCurrentState(layer)));
  }
}

// audioCall(scene, entity, op): 0 play, 1 stop, 2 pause, 3 unpause, 4 isPlaying
void AudioCall(const Info& info) {
  auto* source = ArgComponent<AudioSource>(info);
  int op;
  if (!source || !ArgInt(info, 2, &op)) return;
  switch (op) {
    case 0: source->Play(); break;
    case 1: source->Stop(); break;
    case 2: source->Pause(); break;
    case 3: source->UnPause(); break;
    default: info.GetReturnValue().Set(source->IsPlaying()); break;
  }
}

void AudioPlayOneShot(const Info& info) {
  auto* source = ArgComponent<AudioSource>(info);
  std::string clip;
  double volume;
  if (source && ArgString(info, 2, &clip) && ArgNumber(info, 3, &volume)) {
    source->PlayOneShot(AssetRef{clip}, static_cast<float>(volume));
  }
}

// audioSet(scene, entity, property, value): "volume", "pitch", "loop", "mute", "clip"
void AudioSet(const Info& info) {
  auto* source = ArgComponent<AudioSource>(info);
  std::string property;
  if (!source || !ArgString(info, 2, &property)) return;
  double number = 0;
  bool flag = false;
  std::string text;
  if (property == "volume" && ArgNumber(info, 3, &number)) source->SetVolume(static_cast<float>(number));
  else if (property == "pitch" && ArgNumber(info, 3, &number)) source->SetPitch(static_cast<float>(number));
  else if (property == "loop" && ArgBool(info, 3, &flag)) source->SetLoop(flag);
  else if (property == "mute" && ArgBool(info, 3, &flag)) source->SetMute(flag);
  else if (property == "clip" && ArgString(info, 3, &text)) source->SetClip(AssetRef{text});
}

void CharacterMove(const Info& info) {
  auto* controller = ArgComponent<CharacterController>(info);
  glm::vec3 motion;
  double x, y, z;
  if (!controller || !ArgNumber(info, 2, &x) || !ArgNumber(info, 3, &y) || !ArgNumber(info, 4, &z)) return;
  motion = glm::vec3(x, y, z);
  info.GetReturnValue().Set(static_cast<double>(controller->Move(motion)));
}

void CharacterGrounded(const Info& info) {
  if (auto* controller = ArgComponent<CharacterController>(info)) {
    info.GetReturnValue().Set(controller->IsGrounded());
  }
}

void CharacterVelocity(const Info& info) {
  if (auto* controller = ArgComponent<CharacterController>(info)) {
    glm::vec3 v = controller->GetVelocity();
    Scratch()[0] = v.x;
    Scratch()[1] = v.y;
    Scratch()[2] = v.z;
  }
}

}  // namespace

void InstallComponents(v8::Local<v8::Context> context, v8::Local<v8::Object> native) {
  SetFunction(context, native, "componentHas", ComponentHas);
  SetFunction(context, native, "componentGet", ComponentGet);
  SetFunction(context, native, "componentSet", ComponentSet);
  SetFunction(context, native, "componentAdd", ComponentAdd);
  SetFunction(context, native, "animatorSet", AnimatorSet);
  SetFunction(context, native, "animatorGet", AnimatorGet);
  SetFunction(context, native, "animatorPlay", AnimatorPlay);
  SetFunction(context, native, "animatorState", AnimatorState);
  SetFunction(context, native, "audioCall", AudioCall);
  SetFunction(context, native, "audioPlayOneShot", AudioPlayOneShot);
  SetFunction(context, native, "audioSet", AudioSet);
  SetFunction(context, native, "characterMove", CharacterMove);
  SetFunction(context, native, "characterGrounded", CharacterGrounded);
  SetFunction(context, native, "characterVelocity", CharacterVelocity);
}

}  // namespace Mercury::js
