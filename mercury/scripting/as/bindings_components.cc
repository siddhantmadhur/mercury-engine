/*
 * bindings_components.cc
 *
 * Typed access to the engine's built-in components. A ScriptComponent is a
 * (GameObject, type name) pair, so it stays valid as the TypeRegistry grows.
 */
#include <string>

#include "animation/animator.h"
#include "audio/audio_system.h"
#include "components/components.h"
#include "scripting/as/bindings.h"
#include "scripting/as/bindings_types.h"
#include "scripting/as/script_runtime.h"
#include "scripting/type_registry.h"

namespace Mercury::as {

namespace {

const ComponentInfo* InfoOf(const ScriptComponent& component) {
  return TypeRegistry::Instance().FindComponent(component.type);
}

mtext::Variant GetField(const ScriptComponent& component,
                        const std::string& field) {
  MapScene* scene = SceneArg(component.object);
  const ComponentInfo* info = InfoOf(component);
  if (!scene || !info) return {};
  mtext::Variant value;
  if (!info->get_field ||
      !info->get_field(*scene, component.object.GetEntity(), field, &value)) {
    Throw(("The object has no " + component.type + " component with a field " +
           field)
              .c_str());
    return {};
  }
  return value;
}

void SetField(const ScriptComponent& component, const std::string& field,
              const mtext::Variant& value) {
  MapScene* scene = SceneArg(component.object);
  const ComponentInfo* info = InfoOf(component);
  if (!scene || !info) return;
  const FieldInfo* field_info = info->FindField(field);
  if (!field_info) {
    Throw((component.type + " has no field " + field).c_str());
    return;
  }
  if (!field_info->runtime_writable) {
    Throw((component.type + "." + field + " cannot be changed at run time")
              .c_str());
    return;
  }
  if (!info->set_field ||
      !info->set_field(*scene, component.object.GetEntity(), field, value)) {
    Throw(("The object has no " + component.type + " component").c_str());
  }
}

ScriptComponent MakeComponent(GameObject object, const std::string& type) {
  ScriptComponent component;
  MapScene* scene = SceneArg(object);
  const ComponentInfo* info = TypeRegistry::Instance().FindComponent(type);
  if (!scene || !info) {
    Throw(("Unknown component type " + type).c_str());
    return component;
  }
  if (!info->has || !info->has(*scene, object.GetEntity())) return component;
  component.object = object;
  component.type = type;
  return component;
}

ScriptComponent GetComponent(GameObject object, const std::string& type) {
  return MakeComponent(object, type);
}

bool HasComponent(GameObject object, const std::string& type) {
  MapScene* scene = SceneArg(object);
  const ComponentInfo* info = TypeRegistry::Instance().FindComponent(type);
  return scene && info && info->has && info->has(*scene, object.GetEntity());
}

bool AddComponent(GameObject object, const std::string& type) {
  MapScene* scene = SceneArg(object);
  if (!scene) return false;
  if (!scene->AddComponent(object, type)) {
    Throw(("Could not add " + type).c_str());
    return false;
  }
  return true;
}

void RemoveComponent(GameObject object, const std::string& type) {
  if (MapScene* scene = SceneArg(object)) scene->RemoveComponent(object, type);
}

bool ComponentIsValid(const ScriptComponent& component) {
  return component.object.IsValid() && !component.type.empty();
}

bool ComponentHasField(const ScriptComponent& component,
                       const std::string& field) {
  const ComponentInfo* info = InfoOf(component);
  return info && info->FindField(field);
}

bool GetBool(const ScriptComponent& component, const std::string& field) {
  return GetField(component, field).ToBool().value_or(false);
}
int GetInt(const ScriptComponent& component, const std::string& field) {
  return GetField(component, field).ToInt().value_or(0);
}
float GetFloat(const ScriptComponent& component, const std::string& field) {
  return GetField(component, field).ToFloat().value_or(0.0f);
}
std::string GetString(const ScriptComponent& component,
                      const std::string& field) {
  return GetField(component, field).ToString().value_or("");
}
glm::vec2 GetVector2(const ScriptComponent& component,
                     const std::string& field) {
  auto value = GetField(component, field).Get<glm::vec2>();
  return value ? *value : glm::vec2(0.0f);
}
glm::vec3 GetVector3(const ScriptComponent& component,
                     const std::string& field) {
  auto value = GetField(component, field).Get<glm::vec3>();
  return value ? *value : glm::vec3(0.0f);
}
glm::vec4 GetColor(const ScriptComponent& component, const std::string& field) {
  auto value = GetField(component, field).Get<mtext::Color>();
  return value ? value->rgba : glm::vec4(1.0f);
}

void SetBool(const ScriptComponent& component, const std::string& field,
             bool value) {
  SetField(component, field, mtext::Variant(value));
}
void SetInt(const ScriptComponent& component, const std::string& field,
            int value) {
  SetField(component, field, mtext::Variant(value));
}
void SetFloat(const ScriptComponent& component, const std::string& field,
              float value) {
  SetField(component, field, mtext::Variant(value));
}
void SetString(const ScriptComponent& component, const std::string& field,
               const std::string& value) {
  SetField(component, field, mtext::Variant(value));
}
void SetVector2(const ScriptComponent& component, const std::string& field,
                const glm::vec2& value) {
  SetField(component, field, mtext::Variant(value));
}
void SetVector3(const ScriptComponent& component, const std::string& field,
                const glm::vec3& value) {
  SetField(component, field, mtext::Variant(value));
}
void SetColor(const ScriptComponent& component, const std::string& field,
              const glm::vec4& value) {
  SetField(component, field, mtext::Variant(mtext::Color{value}));
}

// --- Animator
// ------------------------------------------------------------------------------

Animator* AnimatorOf(const ScriptComponent& component) {
  MapScene* scene = SceneArg(component.object);
  Animator* animator =
      scene ? component.object.GetComponent<Animator>() : nullptr;
  if (!animator) Throw("The object has no Animator");
  return animator;
}
void AnimatorSetFloat(const ScriptComponent& component, const std::string& name,
                      float value) {
  if (auto* animator = AnimatorOf(component)) animator->SetFloat(name, value);
}
float AnimatorGetFloat(const ScriptComponent& component,
                       const std::string& name) {
  auto* animator = AnimatorOf(component);
  return animator ? animator->GetFloat(name) : 0.0f;
}
void AnimatorSetInteger(const ScriptComponent& component,
                        const std::string& name, int value) {
  if (auto* animator = AnimatorOf(component)) animator->SetInteger(name, value);
}
int AnimatorGetInteger(const ScriptComponent& component,
                       const std::string& name) {
  auto* animator = AnimatorOf(component);
  return animator ? animator->GetInteger(name) : 0;
}
void AnimatorSetBool(const ScriptComponent& component, const std::string& name,
                     bool value) {
  if (auto* animator = AnimatorOf(component)) animator->SetBool(name, value);
}
bool AnimatorGetBool(const ScriptComponent& component,
                     const std::string& name) {
  auto* animator = AnimatorOf(component);
  return animator && animator->GetBool(name);
}
void AnimatorSetTrigger(const ScriptComponent& component,
                        const std::string& name) {
  if (auto* animator = AnimatorOf(component)) animator->SetTrigger(name);
}
void AnimatorResetTrigger(const ScriptComponent& component,
                          const std::string& name) {
  if (auto* animator = AnimatorOf(component)) animator->ResetTrigger(name);
}
void AnimatorPlay(const ScriptComponent& component, const std::string& state,
                  int layer, float time) {
  if (auto* animator = AnimatorOf(component))
    animator->Play(state, layer, time);
}
std::string AnimatorCurrentState(const ScriptComponent& component, int layer) {
  auto* animator = AnimatorOf(component);
  return animator ? animator->GetCurrentState(layer) : std::string();
}

// --- AudioSource
// ---------------------------------------------------------------------------

AudioSource* AudioOf(const ScriptComponent& component) {
  MapScene* scene = SceneArg(component.object);
  AudioSource* source =
      scene ? component.object.GetComponent<AudioSource>() : nullptr;
  if (!source) Throw("The object has no AudioSource");
  return source;
}
void AudioPlay(const ScriptComponent& component) {
  if (auto* source = AudioOf(component)) source->Play();
}
void AudioStop(const ScriptComponent& component) {
  if (auto* source = AudioOf(component)) source->Stop();
}
void AudioPause(const ScriptComponent& component) {
  if (auto* source = AudioOf(component)) source->Pause();
}
void AudioUnPause(const ScriptComponent& component) {
  if (auto* source = AudioOf(component)) source->UnPause();
}
bool AudioIsPlaying(const ScriptComponent& component) {
  auto* source = AudioOf(component);
  return source && source->IsPlaying();
}
void AudioPlayOneShot(const ScriptComponent& component, const std::string& clip,
                      float volume) {
  if (auto* source = AudioOf(component))
    source->PlayOneShot(AssetRef{clip}, volume);
}
void AudioSetFloat(const ScriptComponent& component,
                   const std::string& property, float value) {
  auto* source = AudioOf(component);
  if (!source) return;
  if (property == "volume")
    source->SetVolume(value);
  else if (property == "pitch")
    source->SetPitch(value);
}
void AudioSetBool(const ScriptComponent& component, const std::string& property,
                  bool value) {
  auto* source = AudioOf(component);
  if (!source) return;
  if (property == "loop")
    source->SetLoop(value);
  else if (property == "mute")
    source->SetMute(value);
}
void AudioSetClip(const ScriptComponent& component, const std::string& clip) {
  if (auto* source = AudioOf(component)) source->SetClip(AssetRef{clip});
}

// --- CharacterController
// -------------------------------------------------------------------

CharacterController* CharacterOf(const ScriptComponent& component) {
  MapScene* scene = SceneArg(component.object);
  CharacterController* controller =
      scene ? component.object.GetComponent<CharacterController>() : nullptr;
  if (!controller) Throw("The object has no CharacterController");
  return controller;
}
float CharacterMove(const ScriptComponent& component, const glm::vec3& motion) {
  auto* controller = CharacterOf(component);
  return controller ? controller->Move(motion) : 0.0f;
}
bool CharacterGrounded(const ScriptComponent& component) {
  auto* controller = CharacterOf(component);
  return controller && controller->IsGrounded();
}
glm::vec3 CharacterVelocity(const ScriptComponent& component) {
  auto* controller = CharacterOf(component);
  return controller ? controller->GetVelocity() : glm::vec3(0.0f);
}

}  // namespace

void InstallComponents(asIScriptEngine* engine) {
  RegisterComplexValueType<ScriptComponent>(engine, "Component");
  engine->RegisterObjectMethod("Component", "bool get_isValid() const property",
                               asFUNCTION(ComponentIsValid),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "string get_typeName() const property",
      asFUNCTION(+[](const ScriptComponent& c) { return c.type; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "bool HasField(const string &in) const",
      asFUNCTION(ComponentHasField), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "bool GetBool(const string &in) const",
                               asFUNCTION(GetBool), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "int GetInt(const string &in) const",
                               asFUNCTION(GetInt), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "float GetFloat(const string &in) const",
                               asFUNCTION(GetFloat), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "string GetString(const string &in) const",
                               asFUNCTION(GetString), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "Vector2 GetVector2(const string &in) const",
                               asFUNCTION(GetVector2), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "Vector3 GetVector3(const string &in) const",
                               asFUNCTION(GetVector3), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "Color GetColor(const string &in) const",
                               asFUNCTION(GetColor), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "void SetBool(const string &in, bool)",
                               asFUNCTION(SetBool), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "void SetInt(const string &in, int)",
                               asFUNCTION(SetInt), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Component",
                               "void SetFloat(const string &in, float)",
                               asFUNCTION(SetFloat), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "void SetString(const string &in, const string &in)",
      asFUNCTION(SetString), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "void SetVector2(const string &in, const Vector2 &in)",
      asFUNCTION(SetVector2), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "void SetVector3(const string &in, const Vector3 &in)",
      asFUNCTION(SetVector3), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Component", "void SetColor(const string &in, const Color &in)",
      asFUNCTION(SetColor), asCALL_CDECL_OBJFIRST);

  engine->SetDefaultNamespace("Components");
  engine->RegisterGlobalFunction("Component Get(GameObject, const string &in)",
                                 asFUNCTION(GetComponent), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool Has(GameObject, const string &in)",
                                 asFUNCTION(HasComponent), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool Add(GameObject, const string &in)",
                                 asFUNCTION(AddComponent), asCALL_CDECL);
  engine->RegisterGlobalFunction("void Remove(GameObject, const string &in)",
                                 asFUNCTION(RemoveComponent), asCALL_CDECL);

  engine->RegisterGlobalFunction(
      "void AnimatorSetFloat(Component, const string &in, float)",
      asFUNCTION(AnimatorSetFloat), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "float AnimatorGetFloat(Component, const string &in)",
      asFUNCTION(AnimatorGetFloat), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AnimatorSetInteger(Component, const string &in, int)",
      asFUNCTION(AnimatorSetInteger), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "int AnimatorGetInteger(Component, const string &in)",
      asFUNCTION(AnimatorGetInteger), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AnimatorSetBool(Component, const string &in, bool)",
      asFUNCTION(AnimatorSetBool), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "bool AnimatorGetBool(Component, const string &in)",
      asFUNCTION(AnimatorGetBool), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AnimatorSetTrigger(Component, const string &in)",
      asFUNCTION(AnimatorSetTrigger), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AnimatorResetTrigger(Component, const string &in)",
      asFUNCTION(AnimatorResetTrigger), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AnimatorPlay(Component, const string &in, int, float)",
      asFUNCTION(AnimatorPlay), asCALL_CDECL);
  engine->RegisterGlobalFunction("string AnimatorCurrentState(Component, int)",
                                 asFUNCTION(AnimatorCurrentState),
                                 asCALL_CDECL);

  engine->RegisterGlobalFunction("void AudioPlay(Component)",
                                 asFUNCTION(AudioPlay), asCALL_CDECL);
  engine->RegisterGlobalFunction("void AudioStop(Component)",
                                 asFUNCTION(AudioStop), asCALL_CDECL);
  engine->RegisterGlobalFunction("void AudioPause(Component)",
                                 asFUNCTION(AudioPause), asCALL_CDECL);
  engine->RegisterGlobalFunction("void AudioUnPause(Component)",
                                 asFUNCTION(AudioUnPause), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool AudioIsPlaying(Component)",
                                 asFUNCTION(AudioIsPlaying), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AudioPlayOneShot(Component, const string &in, float)",
      asFUNCTION(AudioPlayOneShot), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AudioSetFloat(Component, const string &in, float)",
      asFUNCTION(AudioSetFloat), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AudioSetBool(Component, const string &in, bool)",
      asFUNCTION(AudioSetBool), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AudioSetClip(Component, const string &in)",
      asFUNCTION(AudioSetClip), asCALL_CDECL);

  engine->RegisterGlobalFunction(
      "float CharacterMove(Component, const Vector3 &in)",
      asFUNCTION(CharacterMove), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool CharacterGrounded(Component)",
                                 asFUNCTION(CharacterGrounded), asCALL_CDECL);
  engine->RegisterGlobalFunction("Vector3 CharacterVelocity(Component)",
                                 asFUNCTION(CharacterVelocity), asCALL_CDECL);
  engine->SetDefaultNamespace("");
}

}  // namespace Mercury::as
