/*
 * bindings_core.cc
 *
 * Natives for registration, time, random, objects, transforms, scenes, physics, input and
 * debugging. Every native that takes (scene, entity) checks both first (see bindings.h).
 */
#include <imgui.h>

#include <random>

#include "debugger/debugger.h"
#include "math/time.h"
#include "scripting/js/bindings.h"
#include "scripting/js/script_behaviour.h"

namespace Mercury::js {

namespace {

std::mt19937& Rng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

void Return(const Info& info, double value) { info.GetReturnValue().Set(value); }
void Return(const Info& info, bool value) { info.GetReturnValue().Set(value); }
void Return(const Info& info, const std::string& value) {
  info.GetReturnValue().Set(Str(info.GetIsolate(), value));
}

void WriteVec3(glm::vec3 v, int at = 0) {
  double* s = Scratch();
  s[at] = v.x;
  s[at + 1] = v.y;
  s[at + 2] = v.z;
}

bool ArgVec3(const Info& info, int i, glm::vec3* out) {
  double x, y, z;
  if (!ArgNumber(info, i, &x) || !ArgNumber(info, i + 1, &y) || !ArgNumber(info, i + 2, &z)) {
    return false;
  }
  *out = glm::vec3(x, y, z);
  return true;
}

v8::Local<v8::Array> EntityArray(v8::Isolate* isolate, const std::vector<GameObject>& objects) {
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> array = v8::Array::New(isolate, static_cast<int>(objects.size()));
  for (uint32_t i = 0; i < objects.size(); i++) {
    array->Set(context, i, v8::Number::New(isolate, EntityToJs(objects[i].GetEntity()))).Check();
  }
  return array;
}

// --- registration -------------------------------------------------------------------------

std::string StringProp(v8::Local<v8::Context> context, v8::Local<v8::Object> object,
                       const char* name) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Local<v8::Value> value;
  if (!object->Get(context, Str(isolate, name)).ToLocal(&value) || !value->IsString()) return {};
  v8::String::Utf8Value utf8(isolate, value);
  return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

FieldType FieldTypeFromName(const std::string& name) {
  static const std::pair<const char*, FieldType> kTypes[] = {
      {"bool", FieldType::kBool},       {"int", FieldType::kInt},         {"float", FieldType::kFloat},
      {"string", FieldType::kString},   {"vector2", FieldType::kVector2}, {"vector3", FieldType::kVector3},
      {"color", FieldType::kColor},     {"nodeRef", FieldType::kNodeRef}, {"asset", FieldType::kAsset},
      {"enum", FieldType::kEnum},       {"list", FieldType::kList},       {"event", FieldType::kEvent}};
  for (const auto& [text, type] : kTypes) {
    if (name == text) return type;
  }
  return FieldType::kFloat;
}

// __native.registerScript(spec): spec is built by finalizeScripts in the TS runtime.
void RegisterScript(const Info& info) {
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  if (info.Length() < 1 || !info[0]->IsObject()) {
    ThrowType(isolate, "registerScript needs a spec object");
    return;
  }
  v8::Local<v8::Object> spec = info[0].As<v8::Object>();
  auto script_class = std::make_unique<ScriptClass>();
  script_class->name = StringProp(context, spec, "name");
  script_class->location = StringProp(context, spec, "location");

  v8::Local<v8::Value> ctor;
  if (!spec->Get(context, Str(isolate, "ctor")).ToLocal(&ctor) || !ctor->IsFunction()) {
    ThrowType(isolate, "registerScript: ctor must be a class");
    return;
  }
  script_class->constructor.Reset(isolate, ctor.As<v8::Function>());

  v8::Local<v8::Value> mask;
  if (spec->Get(context, Str(isolate, "callbackMask")).ToLocal(&mask) && mask->IsNumber()) {
    script_class->callbacks = static_cast<uint32_t>(mask.As<v8::Number>()->Value());
  }
  v8::Local<v8::Value> data_asset;
  if (spec->Get(context, Str(isolate, "dataAsset")).ToLocal(&data_asset)) {
    script_class->data_asset = data_asset->BooleanValue(isolate);
  }

  v8::Local<v8::Value> fields;
  if (spec->Get(context, Str(isolate, "fields")).ToLocal(&fields) && fields->IsArray()) {
    v8::Local<v8::Array> array = fields.As<v8::Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
      v8::Local<v8::Value> item;
      if (!array->Get(context, i).ToLocal(&item) || !item->IsObject()) continue;
      v8::Local<v8::Object> object = item.As<v8::Object>();
      ScriptField field;
      field.name = StringProp(context, object, "name");
      field.type = FieldTypeFromName(StringProp(context, object, "type"));
      field.element_type = FieldTypeFromName(StringProp(context, object, "elementType"));
      field.asset_type = StringProp(context, object, "assetType");
      field.tooltip = StringProp(context, object, "tooltip");
      v8::Local<v8::Value> values;
      if (object->Get(context, Str(isolate, "enumValues")).ToLocal(&values) && values->IsArray()) {
        v8::Local<v8::Array> names = values.As<v8::Array>();
        for (uint32_t k = 0; k < names->Length(); k++) {
          v8::Local<v8::Value> name;
          if (names->Get(context, k).ToLocal(&name) && name->IsString()) {
            v8::String::Utf8Value utf8(isolate, name);
            field.enum_values.emplace_back(*utf8, static_cast<size_t>(utf8.length()));
          }
        }
      }
      script_class->fields.push_back(std::move(field));
    }
  }
  v8::Local<v8::Value> bindables;
  if (spec->Get(context, Str(isolate, "bindables")).ToLocal(&bindables) && bindables->IsArray()) {
    v8::Local<v8::Array> array = bindables.As<v8::Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
      v8::Local<v8::Value> name;
      if (array->Get(context, i).ToLocal(&name) && name->IsString()) {
        v8::String::Utf8Value utf8(isolate, name);
        script_class->bindables.emplace_back(*utf8, static_cast<size_t>(utf8.length()));
      }
    }
  }
  v8::Local<v8::Value> callables;
  if (spec->Get(context, Str(isolate, "callables")).ToLocal(&callables) && callables->IsArray()) {
    v8::Local<v8::Array> array = callables.As<v8::Array>();
    for (uint32_t i = 0; i < array->Length(); i++) {
      v8::Local<v8::Value> item;
      if (!array->Get(context, i).ToLocal(&item) || !item->IsObject()) continue;
      script_class->callables.push_back(
          {StringProp(context, item.As<v8::Object>(), "name"),
           StringProp(context, item.As<v8::Object>(), "arg")});
    }
  }

  std::string error;
  if (!ScriptRuntime::Instance().AddScriptClass(std::move(script_class), &error)) {
    Throw(isolate, error);
  }
}

// __native.setHelper(name, fn): the TS runtime hands over functions C++ needs to call.
void SetHelperNative(const Info& info) {
  std::string name;
  if (!ArgString(info, 0, &name)) return;
  if (info.Length() < 2 || !info[1]->IsFunction()) {
    ThrowType(info.GetIsolate(), "setHelper needs a function");
    return;
  }
  ScriptRuntime::Instance().SetHelper(name, info[1].As<v8::Function>());
}

// __native.mapStack(stack): rewrites bundle positions in a stack string as TypeScript ones.
void MapStack(const Info& info) {
  std::string stack;
  if (!ArgString(info, 0, &stack)) return;
  Return(info, ScriptRuntime::Instance().RemapStack(stack));
}

// --- time, random, debug ------------------------------------------------------------------

void EventInvoke(const Info& info) {
  int id;
  std::string field;
  if (!ArgInt(info, 0, &id) || !ArgString(info, 1, &field)) return;
  ScriptBehaviour* behaviour = ScriptRuntime::Instance().FindBehaviour(id);
  if (!behaviour) return;
  if (Event* event = behaviour->FindEvent(field)) event->Invoke();
}

void LogNative(const Info& info) {
  int level;
  std::string text;
  if (!ArgInt(info, 0, &level) || !ArgString(info, 1, &text)) return;
  switch (level) {
    case 0: PDebug::log("{}", text); break;
    case 1: PDebug::info("{}", text); break;
    case 2: PDebug::warn("{}", text); break;
    default: PDebug::error("{}", text); break;
  }
}

// --- objects ------------------------------------------------------------------------------

void ObjectIsValid(const Info& info) {
  double serial, entity;
  if (!ArgNumber(info, 0, &serial) || !ArgNumber(info, 1, &entity)) return;
  MapScene* scene = ScriptRuntime::Instance().FindScene(static_cast<uint32_t>(serial));
  Return(info, scene && GameObject(scene, static_cast<kEntityId>(static_cast<uint32_t>(entity))).IsValid());
}

void ObjectName(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, o->GetName());
}
void ObjectPath(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, o->GetPath());
}
void ObjectTag(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, o->GetTag());
}
void ObjectSetTag(const Info& info) {
  auto o = ArgGameObject(info, 0);
  std::string tag;
  if (o && ArgString(info, 2, &tag)) o->SetTag(tag);
}
void ObjectLayer(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, static_cast<double>(o->GetLayer()));
}
void ObjectSetLayer(const Info& info) {
  auto o = ArgGameObject(info, 0);
  int layer;
  if (o && ArgInt(info, 2, &layer)) o->SetLayer(layer);
}
void ObjectActiveSelf(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, o->IsActiveSelf());
}
void ObjectActiveInHierarchy(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) Return(info, o->IsActiveInHierarchy());
}
void ObjectSetActive(const Info& info) {
  auto o = ArgGameObject(info, 0);
  bool active;
  if (o && ArgBool(info, 2, &active)) o->SetActive(active);
}
void ObjectParent(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) {
    GameObject parent = o->GetParent();
    Return(info, parent ? EntityToJs(parent.GetEntity()) : -1.0);
  }
}
void ObjectChildren(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) {
    info.GetReturnValue().Set(EntityArray(info.GetIsolate(), o->GetChildren()));
  }
}
void ObjectFindChild(const Info& info) {
  auto o = ArgGameObject(info, 0);
  std::string path;
  if (!o || !ArgString(info, 2, &path)) return;
  GameObject child = o->FindChild(path);
  Return(info, child ? EntityToJs(child.GetEntity()) : -1.0);
}
void ObjectSetParent(const Info& info) {
  auto o = ArgGameObject(info, 0);
  double parent;
  bool keep;
  if (!o || !ArgNumber(info, 2, &parent) || !ArgBool(info, 3, &keep)) return;
  GameObject new_parent;
  if (parent >= 0) {
    new_parent = GameObject(o->GetScene(), static_cast<kEntityId>(static_cast<uint32_t>(parent)));
    if (!new_parent.IsValid()) {
      Throw(info.GetIsolate(), "The new parent was destroyed");
      return;
    }
  }
  o->SetParent(new_parent, keep);
}
// objectBounds(scene, entity) -> bool; the mesh bounds (entity space) go to scratch[0..5].
void ObjectBounds(const Info& info) {
  auto o = ArgGameObject(info, 0);
  if (!o) return;
  auto* mesh = o->GetComponent<RenderMesh>();
  if (!mesh) {
    Return(info, false);
    return;
  }
  WriteVec3(mesh->bounds_min, 0);
  WriteVec3(mesh->bounds_max, 3);
  Return(info, true);
}
void ObjectDestroy(const Info& info) {
  if (auto o = ArgGameObject(info, 0)) o->Destroy();
}

// --- transform ----------------------------------------------------------------------------

Transform* ArgTransform(const Info& info, std::optional<GameObject>* holder) {
  *holder = ArgGameObject(info, 0);
  if (!*holder) return nullptr;
  Transform* transform = (*holder)->GetTransform();
  if (!transform) Throw(info.GetIsolate(), "The object has no Transform");
  return transform;
}

void TransformGetPosition(const Info& info) {
  std::optional<GameObject> o;
  if (Transform* t = ArgTransform(info, &o)) WriteVec3(t->GetPosition());
}
void TransformSetPosition(const Info& info) {
  std::optional<GameObject> o;
  glm::vec3 v;
  Transform* t = ArgTransform(info, &o);
  if (t && ArgVec3(info, 2, &v)) t->SetPosition(v);
}
void TransformGetRotation(const Info& info) {
  std::optional<GameObject> o;
  if (Transform* t = ArgTransform(info, &o)) {
    glm::quat q = t->GetRotationQuat();
    double* s = Scratch();
    s[0] = q.x;
    s[1] = q.y;
    s[2] = q.z;
    s[3] = q.w;
  }
}
void TransformSetRotation(const Info& info) {
  std::optional<GameObject> o;
  Transform* t = ArgTransform(info, &o);
  double x, y, z, w;
  if (!t || !ArgNumber(info, 2, &x) || !ArgNumber(info, 3, &y) || !ArgNumber(info, 4, &z) ||
      !ArgNumber(info, 5, &w)) {
    return;
  }
  t->SetRotate(Transform::QuatToEuler(glm::quat(static_cast<float>(w), static_cast<float>(x),
                                                static_cast<float>(y), static_cast<float>(z))));
}
void TransformGetEuler(const Info& info) {
  std::optional<GameObject> o;
  if (Transform* t = ArgTransform(info, &o)) WriteVec3(t->GetRotate());
}
void TransformSetEuler(const Info& info) {
  std::optional<GameObject> o;
  glm::vec3 v;
  Transform* t = ArgTransform(info, &o);
  if (t && ArgVec3(info, 2, &v)) t->SetRotate(v);
}
void TransformGetScale(const Info& info) {
  std::optional<GameObject> o;
  if (Transform* t = ArgTransform(info, &o)) WriteVec3(t->GetLocalScale());
}
void TransformSetScale(const Info& info) {
  std::optional<GameObject> o;
  glm::vec3 v;
  Transform* t = ArgTransform(info, &o);
  if (t && ArgVec3(info, 2, &v)) t->SetLocalScale(v);
}
void TransformGetVelocity(const Info& info) {
  std::optional<GameObject> o;
  if (Transform* t = ArgTransform(info, &o)) WriteVec3(t->GetVelocity());
}
void TransformSetVelocity(const Info& info) {
  std::optional<GameObject> o;
  glm::vec3 v;
  Transform* t = ArgTransform(info, &o);
  if (t && ArgVec3(info, 2, &v)) t->SetVelocity(v);
}

// --- scene --------------------------------------------------------------------------------

void SceneFind(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string path;
  if (!scene || !ArgString(info, 1, &path)) return;
  GameObject found = scene->Find(path);
  Return(info, found ? EntityToJs(found.GetEntity()) : -1.0);
}
void SceneFindWithTag(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string tag;
  if (!scene || !ArgString(info, 1, &tag)) return;
  GameObject found = scene->FindWithTag(tag);
  Return(info, found ? EntityToJs(found.GetEntity()) : -1.0);
}
void SceneFindAllWithTag(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string tag;
  if (!scene || !ArgString(info, 1, &tag)) return;
  info.GetReturnValue().Set(EntityArray(info.GetIsolate(), scene->FindGameObjectsWithTag(tag)));
}
void SceneInstantiate(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string path;
  double qx, qy, qz, qw, parent;
  glm::vec3 position;
  if (!scene || !ArgString(info, 1, &path) || !ArgVec3(info, 2, &position) ||
      !ArgNumber(info, 5, &qx) || !ArgNumber(info, 6, &qy) || !ArgNumber(info, 7, &qz) ||
      !ArgNumber(info, 8, &qw) || !ArgNumber(info, 9, &parent)) {
    return;
  }
  GameObject parent_object;
  if (parent >= 0) {
    parent_object = GameObject(scene, static_cast<kEntityId>(static_cast<uint32_t>(parent)));
    if (!parent_object.IsValid()) {
      Throw(info.GetIsolate(), "The parent was destroyed");
      return;
    }
  }
  GameObject created = scene->InstantiatePrefab(
      path, position,
      glm::quat(static_cast<float>(qw), static_cast<float>(qx), static_cast<float>(qy),
                static_cast<float>(qz)),
      parent_object);
  if (!created) {
    Throw(info.GetIsolate(), "Could not instantiate " + path);
    return;
  }
  Return(info, EntityToJs(created.GetEntity()));
}
void SceneCreate(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string name;
  double parent;
  if (!scene || !ArgString(info, 1, &name) || !ArgNumber(info, 2, &parent)) return;
  GameObject parent_object;
  if (parent >= 0) parent_object = GameObject(scene, static_cast<kEntityId>(static_cast<uint32_t>(parent)));
  GameObject created = scene->CreateGameObject(name, parent_object);
  Return(info, EntityToJs(created.GetEntity()));
}
void SceneLoadMap(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string path;
  if (scene && ArgString(info, 1, &path)) scene->LoadMap(path);
}
void SceneQuit(const Info& info) {
  if (MapScene* scene = ArgScene(info, 0)) scene->QuitScene();
}

// The script instances on one object (an array of JS objects).
void SceneBehaviours(const Info& info) {
  auto o = ArgGameObject(info, 0);
  if (!o) return;
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> array = v8::Array::New(isolate);
  uint32_t n = 0;
  for (Behaviour* behaviour : o->GetScene()->GetBehaviours(o->GetEntity())) {
    if (auto* script = dynamic_cast<ScriptBehaviour*>(behaviour)) {
      array->Set(context, n++, script->Instance()).Check();
    }
  }
  info.GetReturnValue().Set(array);
}

// Every script instance on an active object of the scene.
void SceneAllBehaviours(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  if (!scene) return;
  v8::Isolate* isolate = info.GetIsolate();
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  v8::Local<v8::Array> array = v8::Array::New(isolate);
  uint32_t n = 0;
  for (kEntityId entity : scene->GetRegistry().view<NodeInfo>()) {
    if (!scene->IsActiveInHierarchy(entity)) continue;
    for (Behaviour* behaviour : scene->GetBehaviours(entity)) {
      if (auto* script = dynamic_cast<ScriptBehaviour*>(behaviour)) {
        array->Set(context, n++, script->Instance()).Check();
      }
    }
  }
  info.GetReturnValue().Set(array);
}

// --- behaviour state ----------------------------------------------------------------------

void BehaviourSetEnabled(const Info& info) {
  int id;
  bool enabled;
  if (!ArgInt(info, 0, &id) || !ArgBool(info, 1, &enabled)) return;
  if (ScriptBehaviour* b = ScriptRuntime::Instance().FindBehaviour(id)) b->SetEnabled(enabled);
}
void BehaviourIsEnabled(const Info& info) {
  int id;
  if (!ArgInt(info, 0, &id)) return;
  ScriptBehaviour* b = ScriptRuntime::Instance().FindBehaviour(id);
  Return(info, b && b->IsEnabled());
}

// --- physics ------------------------------------------------------------------------------

void PhysicsForce(const Info& info, int kind) {
  auto o = ArgGameObject(info, 0);
  glm::vec3 v;
  int mode = 0;
  if (!o || !ArgVec3(info, 2, &v)) return;
  if (info.Length() > 5) ArgInt(info, 5, &mode);
  MapScene* scene = o->GetScene();
  switch (kind) {
    case 0: scene->Physics().AddForce(o->GetEntity(), v, static_cast<ForceMode>(mode)); break;
    case 1: scene->Physics().AddImpulse(o->GetEntity(), v); break;
    case 2: scene->Physics().AddTorque(o->GetEntity(), v, static_cast<ForceMode>(mode)); break;
    default: scene->Physics().SetAngularVelocity(o->GetEntity(), v); break;
  }
}
void PhysicsAddForce(const Info& info) { PhysicsForce(info, 0); }
void PhysicsAddImpulse(const Info& info) { PhysicsForce(info, 1); }
void PhysicsAddTorque(const Info& info) { PhysicsForce(info, 2); }
void PhysicsSetAngularVelocity(const Info& info) { PhysicsForce(info, 3); }

// raycast(scene, ox,oy,oz, dx,dy,dz, maxDistance, layerMask, queryTriggers) -> entity or -1;
// the hit point, normal and distance go to scratch[0..2], [3..5] and [6].
void PhysicsRaycast(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  glm::vec3 origin, direction;
  double max_distance, mask;
  bool triggers;
  if (!scene || !ArgVec3(info, 1, &origin) || !ArgVec3(info, 4, &direction) ||
      !ArgNumber(info, 7, &max_distance) || !ArgNumber(info, 8, &mask) ||
      !ArgBool(info, 9, &triggers)) {
    return;
  }
  auto hit = scene->Physics().Raycast(origin, direction, static_cast<float>(max_distance),
                                      static_cast<uint32_t>(mask), triggers);
  if (!hit) {
    Return(info, -1.0);
    return;
  }
  WriteVec3(hit->point, 0);
  WriteVec3(hit->normal, 3);
  Scratch()[6] = glm::length(hit->point - origin);
  Return(info, EntityToJs(hit->entity));
}

// --- input --------------------------------------------------------------------------------

void InputKey(const Info& info, int kind) {
  MapScene* scene = ArgScene(info, 0);
  int key;
  if (!scene || !ArgInt(info, 1, &key)) return;
  InputManager* input = scene->Input();
  bool result = false;
  if (input) result = kind == 0 ? input->IsPressed(static_cast<Key>(key)) : input->IsJustPressed(static_cast<Key>(key));
  Return(info, result);
}
void InputIsKeyPressed(const Info& info) { InputKey(info, 0); }
void InputIsKeyJustPressed(const Info& info) { InputKey(info, 1); }

void InputMouseButton(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  int button;
  if (!scene || !ArgInt(info, 1, &button)) return;
  InputManager* input = scene->Input();
  Return(info, input && button >= 0 && button <= 2 &&
                   input->IsPressed(static_cast<MouseButton>(button)));
}
void InputPointerOverUi(const Info& info) {
  if (MapScene* scene = ArgScene(info, 0)) Return(info, scene->GetUI().IsPointerOverUI());
}

void InputCursorDelta(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  if (!scene || !scene->Input()) return;
  glm::vec2 d = scene->Input()->GetCursorDelta();
  Scratch()[0] = d.x;
  Scratch()[1] = d.y;
}
void InputFrameScroll(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  if (!scene || !scene->Input()) return;
  glm::vec2 d = scene->Input()->GetFrameScroll();
  Scratch()[0] = d.x;
  Scratch()[1] = d.y;
}

// inputActionFind(scene, "Map/Action") -> index or -1
void InputActionFind(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string name;
  if (!scene || !ArgString(info, 1, &name)) return;
  InputActionAsset* asset = scene->GetInputActions();
  InputAction* action = asset ? asset->Find(name) : nullptr;
  if (!action) {
    Return(info, -1.0);
    return;
  }
  const auto& actions = asset->GetActions();
  for (size_t i = 0; i < actions.size(); i++) {
    if (actions[i].get() == action) {
      Return(info, static_cast<double>(i));
      return;
    }
  }
  Return(info, -1.0);
}

InputAction* ArgAction(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  int index;
  if (!scene || !ArgInt(info, 1, &index)) return nullptr;
  InputActionAsset* asset = scene->GetInputActions();
  if (!asset || index < 0 || static_cast<size_t>(index) >= asset->GetActions().size()) {
    Throw(info.GetIsolate(), "The input action no longer exists");
    return nullptr;
  }
  return asset->GetActions()[index].get();
}
// inputActionRead(scene, index): float in the return value, vec2 also in scratch[0..1]
void InputActionRead(const Info& info) {
  InputAction* action = ArgAction(info);
  if (!action) return;
  glm::vec2 v = action->ReadValue<glm::vec2>();
  Scratch()[0] = v.x;
  Scratch()[1] = v.y;
  Return(info, static_cast<double>(action->ReadValue<float>()));
}
void InputActionState(const Info& info) {
  InputAction* action = ArgAction(info);
  int which;
  if (!action || !ArgInt(info, 2, &which)) return;
  switch (which) {
    case 0: Return(info, action->IsPressed()); break;
    case 1: Return(info, action->WasPressedThisFrame()); break;
    default: Return(info, action->WasReleasedThisFrame()); break;
  }
}
void InputActionEnable(const Info& info) {
  InputAction* action = ArgAction(info);
  bool enable;
  if (!action || !ArgBool(info, 2, &enable)) return;
  if (enable) action->Enable(); else action->Disable();
}

void InputMapEnable(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string map;
  bool enable;
  if (!scene || !ArgString(info, 1, &map) || !ArgBool(info, 2, &enable)) return;
  InputActionAsset* asset = scene->GetInputActions();
  if (!asset) return;
  if (enable) asset->EnableMap(map); else asset->DisableMap(map);
}

// loadDataAsset(scene, className, path) -> an instance of the @dataAsset class filled from the
// .masset file.
void LoadDataAsset(const Info& info) {
  MapScene* scene = ArgScene(info, 0);
  std::string class_name, path;
  if (!scene || !ArgString(info, 1, &class_name) || !ArgString(info, 2, &path)) return;
  const ScriptClass* script_class = ScriptRuntime::Instance().FindScriptClass(class_name);
  if (!script_class || !script_class->data_asset) {
    Throw(info.GetIsolate(), class_name + " is not a @dataAsset class");
    return;
  }
  auto asset = scene->Assets().Load(path);
  if (!asset) {
    Throw(info.GetIsolate(), "Could not load " + path);
    return;
  }
  auto holder = ScriptBehaviour::Create(*script_class, nullptr, entt::null);
  if (!holder) return;
  auto reflector = Reflector::Read(asset->properties);
  holder->Reflect(reflector);
  LogReflectionWarnings(reflector, asset->path);
  info.GetReturnValue().Set(holder->Instance());
}

// --- debug window -------------------------------------------------------------------------

void DebugBegin(const Info& info) {
  std::string title;
  double x, y;
  if (!ArgString(info, 0, &title) || !ArgNumber(info, 1, &x) || !ArgNumber(info, 2, &y)) return;
  ImGui::SetNextWindowPos(ImVec2(static_cast<float>(x), static_cast<float>(y)),
                          ImGuiCond_FirstUseEver);
  ImGui::Begin(title.c_str());
}
void DebugText(const Info& info) {
  std::string text;
  if (ArgString(info, 0, &text)) ImGui::TextUnformatted(text.c_str());
}
void DebugEnd(const Info&) { ImGui::End(); }

}  // namespace

void InstallCore(v8::Local<v8::Context> context, v8::Local<v8::Object> native) {
  // registration
  SetFunction(context, native, "registerScript", RegisterScript);
  SetFunction(context, native, "setHelper", SetHelperNative);
  SetFunction(context, native, "mapStack", MapStack);
  SetFunction(context, native, "eventInvoke", EventInvoke);
  // time, random, log
  SetFunction(context, native, "randomSeed", [](const Info& info) {
    double seed;
    if (ArgNumber(info, 0, &seed)) Rng().seed(static_cast<uint32_t>(seed));
  });
  SetFunction(context, native, "random", [](const Info& info) {
    Return(info, std::uniform_real_distribution<double>(0.0, 1.0)(Rng()));
  });
  SetFunction(context, native, "time", [](const Info& info) { Return(info, Time::GetTime()); });
  SetFunction(context, native, "deltaTime",
              [](const Info& info) { Return(info, static_cast<double>(Time::GetDeltaTime())); });
  SetFunction(context, native, "fixedDeltaTime",
              [](const Info& info) { Return(info, static_cast<double>(Time::GetFixedDeltaTime())); });
  SetFunction(context, native, "realtime",
              [](const Info& info) { Return(info, Time::GetRealtimeSinceStartup()); });
  SetFunction(context, native, "frameCount",
              [](const Info& info) { Return(info, static_cast<double>(Time::GetFrameCount())); });
  SetFunction(context, native, "timeScale",
              [](const Info& info) { Return(info, static_cast<double>(Time::GetTimeScale())); });
  SetFunction(context, native, "setTimeScale", [](const Info& info) {
    double scale;
    if (ArgNumber(info, 0, &scale)) Time::SetTimeScale(static_cast<float>(scale));
  });
  SetFunction(context, native, "log", LogNative);
  // objects
  SetFunction(context, native, "objectIsValid", ObjectIsValid);
  SetFunction(context, native, "objectName", ObjectName);
  SetFunction(context, native, "objectPath", ObjectPath);
  SetFunction(context, native, "objectTag", ObjectTag);
  SetFunction(context, native, "objectSetTag", ObjectSetTag);
  SetFunction(context, native, "objectLayer", ObjectLayer);
  SetFunction(context, native, "objectSetLayer", ObjectSetLayer);
  SetFunction(context, native, "objectActiveSelf", ObjectActiveSelf);
  SetFunction(context, native, "objectActiveInHierarchy", ObjectActiveInHierarchy);
  SetFunction(context, native, "objectSetActive", ObjectSetActive);
  SetFunction(context, native, "objectParent", ObjectParent);
  SetFunction(context, native, "objectChildren", ObjectChildren);
  SetFunction(context, native, "objectFindChild", ObjectFindChild);
  SetFunction(context, native, "objectSetParent", ObjectSetParent);
  SetFunction(context, native, "objectDestroy", ObjectDestroy);
  SetFunction(context, native, "objectBounds", ObjectBounds);
  // transform
  SetFunction(context, native, "getPosition", TransformGetPosition);
  SetFunction(context, native, "setPosition", TransformSetPosition);
  SetFunction(context, native, "getRotation", TransformGetRotation);
  SetFunction(context, native, "setRotation", TransformSetRotation);
  SetFunction(context, native, "getEuler", TransformGetEuler);
  SetFunction(context, native, "setEuler", TransformSetEuler);
  SetFunction(context, native, "getScale", TransformGetScale);
  SetFunction(context, native, "setScale", TransformSetScale);
  SetFunction(context, native, "getVelocity", TransformGetVelocity);
  SetFunction(context, native, "setVelocity", TransformSetVelocity);
  // scene
  SetFunction(context, native, "sceneCurrent", [](const Info& info) {
    Return(info, static_cast<double>(ScriptRuntime::Instance().CurrentScene()));
  });
  SetFunction(context, native, "sceneFind", SceneFind);
  SetFunction(context, native, "sceneFindWithTag", SceneFindWithTag);
  SetFunction(context, native, "sceneFindAllWithTag", SceneFindAllWithTag);
  SetFunction(context, native, "sceneInstantiate", SceneInstantiate);
  SetFunction(context, native, "sceneCreate", SceneCreate);
  SetFunction(context, native, "sceneLoadMap", SceneLoadMap);
  SetFunction(context, native, "sceneQuit", SceneQuit);
  SetFunction(context, native, "objectBehaviours", SceneBehaviours);
  SetFunction(context, native, "sceneAllBehaviours", SceneAllBehaviours);
  SetFunction(context, native, "behaviourSetEnabled", BehaviourSetEnabled);
  SetFunction(context, native, "behaviourIsEnabled", BehaviourIsEnabled);
  // physics
  SetFunction(context, native, "addForce", PhysicsAddForce);
  SetFunction(context, native, "addImpulse", PhysicsAddImpulse);
  SetFunction(context, native, "addTorque", PhysicsAddTorque);
  SetFunction(context, native, "setAngularVelocity", PhysicsSetAngularVelocity);
  SetFunction(context, native, "raycast", PhysicsRaycast);
  // input
  SetFunction(context, native, "isKeyPressed", InputIsKeyPressed);
  SetFunction(context, native, "isKeyJustPressed", InputIsKeyJustPressed);
  SetFunction(context, native, "isMouseButtonPressed", InputMouseButton);
  SetFunction(context, native, "isPointerOverUI", InputPointerOverUi);
  SetFunction(context, native, "cursorDelta", InputCursorDelta);
  SetFunction(context, native, "frameScroll", InputFrameScroll);
  SetFunction(context, native, "inputActionFind", InputActionFind);
  SetFunction(context, native, "inputActionRead", InputActionRead);
  SetFunction(context, native, "inputActionState", InputActionState);
  SetFunction(context, native, "inputActionEnable", InputActionEnable);
  SetFunction(context, native, "inputMapEnable", InputMapEnable);
  SetFunction(context, native, "loadDataAsset", LoadDataAsset);
  // debug window
  SetFunction(context, native, "debugBegin", DebugBegin);
  SetFunction(context, native, "debugText", DebugText);
  SetFunction(context, native, "debugEnd", DebugEnd);
}

}  // namespace Mercury::js
