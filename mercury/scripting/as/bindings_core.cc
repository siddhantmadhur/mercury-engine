/*
 * bindings_core.cc
 *
 * Native value types and the core API: objects, transforms, scenes, physics,
 * input, time, random, math, debug, events and data assets.
 */
#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <string>

#include "debugger/debugger.h"
#include "input/input_actions.h"
#include "input/input_manager.h"
#include "math/mathf.h"
#include "math/time.h"
#include "physics_engine/physics_engine.h"
#include "properties/render_mesh.h"
#include "properties/transform.h"
#include "scripting/as/bindings.h"
#include "scripting/as/bindings_types.h"
#include "scripting/as/script_behaviour.h"
#include "scripting/as/script_event.h"
#include "scripting/as/script_runtime.h"
#include "scriptarray/scriptarray.h"

namespace Mercury::as {

namespace {

std::mt19937& Rng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

MapScene* CurrentScene() {
  return Runtime().FindScene(Runtime().CurrentScene());
}

CScriptArray* MakeObjectArray(const std::vector<GameObject>& objects) {
  asIScriptContext* context = asGetActiveContext();
  if (!context) return nullptr;
  asITypeInfo* type =
      context->GetEngine()->GetTypeInfoByDecl("array<GameObject>");
  if (!type) return nullptr;
  CScriptArray* array =
      CScriptArray::Create(type, static_cast<asUINT>(objects.size()));
  for (asUINT i = 0; i < objects.size(); i++)
    array->SetValue(i, const_cast<GameObject*>(&objects[i]));
  return array;
}

// --- value type constructors
// ---------------------------------------------------------------

void ConstructVector2(glm::vec2* self, float x, float y) {
  new (self) glm::vec2(x, y);
}
void ConstructVector3(glm::vec3* self, float x, float y, float z) {
  new (self) glm::vec3(x, y, z);
}
void ConstructQuaternion(glm::quat* self, float x, float y, float z, float w) {
  new (self) glm::quat(w, x, y, z);
}
void ConstructColor(glm::vec4* self, float r, float g, float b, float a) {
  new (self) glm::vec4(r, g, b, a);
}

// --- vectors
// -------------------------------------------------------------------------------

float Vec2X(const glm::vec2& v) { return v.x; }
float Vec2Y(const glm::vec2& v) { return v.y; }
void SetVec2X(glm::vec2& v, float x) { v.x = x; }
void SetVec2Y(glm::vec2& v, float y) { v.y = y; }
float Vec2Length(const glm::vec2& v) { return glm::length(v); }
float Vec2SqrLength(const glm::vec2& v) { return glm::dot(v, v); }
glm::vec2 Vec2Normalized(const glm::vec2& v) {
  const float length = glm::length(v);
  return length > 0.0f ? v / length : glm::vec2(0.0f);
}
float Vec2Dot(const glm::vec2& a, const glm::vec2& b) { return glm::dot(a, b); }

float Vec3X(const glm::vec3& v) { return v.x; }
float Vec3Y(const glm::vec3& v) { return v.y; }
float Vec3Z(const glm::vec3& v) { return v.z; }
void SetVec3X(glm::vec3& v, float x) { v.x = x; }
void SetVec3Y(glm::vec3& v, float y) { v.y = y; }
void SetVec3Z(glm::vec3& v, float z) { v.z = z; }
float Vec3Length(const glm::vec3& v) { return glm::length(v); }
float Vec3SqrLength(const glm::vec3& v) { return glm::dot(v, v); }
glm::vec3 Vec3Normalized(const glm::vec3& v) {
  const float length = glm::length(v);
  return length > 0.0f ? v / length : glm::vec3(0.0f);
}
float Vec3Dot(const glm::vec3& a, const glm::vec3& b) { return glm::dot(a, b); }
glm::vec3 Vec3Cross(const glm::vec3& a, const glm::vec3& b) {
  return glm::cross(a, b);
}
glm::vec3 Vec3Scale(const glm::vec3& v, float scale) { return v * scale; }

float QuatX(const glm::quat& q) { return q.x; }
float QuatY(const glm::quat& q) { return q.y; }
float QuatZ(const glm::quat& q) { return q.z; }
float QuatW(const glm::quat& q) { return q.w; }
glm::quat QuatMul(const glm::quat& a, const glm::quat& b) { return a * b; }
glm::quat QuatNormalized(const glm::quat& q) { return glm::normalize(q); }
glm::vec3 QuatRotate(const glm::quat& q, const glm::vec3& v) { return q * v; }
glm::quat QuatSlerp(const glm::quat& a, const glm::quat& b, float t) {
  return glm::slerp(a, b, t);
}
glm::quat QuatEuler(float x, float y, float z) {
  return Transform::EulerToQuat({x, y, z});
}

float ColorR(const glm::vec4& c) { return c.r; }
float ColorG(const glm::vec4& c) { return c.g; }
float ColorB(const glm::vec4& c) { return c.b; }
float ColorA(const glm::vec4& c) { return c.a; }
void SetColorR(glm::vec4& c, float v) { c.r = v; }
void SetColorG(glm::vec4& c, float v) { c.g = v; }
void SetColorB(glm::vec4& c, float v) { c.b = v; }
void SetColorA(glm::vec4& c, float v) { c.a = v; }

// --- objects
// -------------------------------------------------------------------------------

std::string ObjectGetName(const GameObject& object) {
  if (!SceneArg(object)) return {};
  return object.GetName();
}
std::string ObjectGetPath(const GameObject& object) {
  if (!SceneArg(object)) return {};
  return object.GetPath();
}
std::string ObjectGetTag(const GameObject& object) {
  if (!SceneArg(object)) return {};
  return object.GetTag();
}
void ObjectSetTag(const GameObject& object, const std::string& tag) {
  if (SceneArg(object)) object.SetTag(tag);
}
int ObjectGetLayer(const GameObject& object) {
  if (!SceneArg(object)) return 0;
  return object.GetLayer();
}
void ObjectSetLayer(const GameObject& object, int layer) {
  if (SceneArg(object)) object.SetLayer(layer);
}
bool ObjectIsValid(const GameObject& object) { return object.IsValid(); }
bool ObjectActiveSelf(const GameObject& object) {
  if (!SceneArg(object)) return false;
  return object.IsActiveSelf();
}
bool ObjectActiveInHierarchy(const GameObject& object) {
  if (!SceneArg(object)) return false;
  return object.IsActiveInHierarchy();
}
void ObjectSetActive(const GameObject& object, bool active) {
  if (SceneArg(object)) object.SetActive(active);
}
GameObject ObjectGetParent(const GameObject& object) {
  if (!SceneArg(object)) return {};
  return object.GetParent();
}
CScriptArray* ObjectGetChildren(const GameObject& object) {
  if (!SceneArg(object)) return MakeObjectArray({});
  return MakeObjectArray(object.GetChildren());
}
GameObject ObjectFindChild(const GameObject& object, const std::string& path) {
  if (!SceneArg(object)) return {};
  return object.FindChild(path);
}
void ObjectSetParent(GameObject& object, GameObject parent,
                     bool keep_world_transform) {
  if (!SceneArg(object)) return;
  if (parent && !parent.IsValid()) {
    Throw("The new parent was destroyed");
    return;
  }
  object.SetParent(parent, keep_world_transform);
}
bool ObjectHasBounds(const GameObject& object) {
  if (!SceneArg(object)) return false;
  return object.GetComponent<RenderMesh>() != nullptr;
}
glm::vec3 ObjectBoundsMin(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(0.0f);
  auto* mesh = object.GetComponent<RenderMesh>();
  return mesh ? mesh->bounds_min : glm::vec3(0.0f);
}
glm::vec3 ObjectBoundsMax(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(0.0f);
  auto* mesh = object.GetComponent<RenderMesh>();
  return mesh ? mesh->bounds_max : glm::vec3(0.0f);
}
void ObjectDestroy(const GameObject& object) {
  if (SceneArg(object)) object.Destroy();
}

glm::vec3 ObjectGetPosition(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(0.0f);
  Transform* transform = object.GetTransform();
  return transform ? transform->GetPosition() : glm::vec3(0.0f);
}
void ObjectSetPosition(const GameObject& object, const glm::vec3& position) {
  if (!SceneArg(object)) return;
  if (Transform* transform = object.GetTransform())
    transform->SetPosition(position);
}
glm::quat ObjectGetRotation(const GameObject& object) {
  if (!SceneArg(object)) return glm::quat(1, 0, 0, 0);
  Transform* transform = object.GetTransform();
  return transform ? transform->GetRotationQuat() : glm::quat(1, 0, 0, 0);
}
void ObjectSetRotation(const GameObject& object, const glm::quat& rotation) {
  if (!SceneArg(object)) return;
  if (Transform* transform = object.GetTransform()) {
    transform->SetRotate(Transform::QuatToEuler(rotation));
  }
}
glm::vec3 ObjectGetEuler(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(0.0f);
  Transform* transform = object.GetTransform();
  return transform ? transform->GetRotate() : glm::vec3(0.0f);
}
void ObjectSetEuler(const GameObject& object, const glm::vec3& euler) {
  if (!SceneArg(object)) return;
  if (Transform* transform = object.GetTransform()) transform->SetRotate(euler);
}
glm::vec3 ObjectGetScale(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(1.0f);
  Transform* transform = object.GetTransform();
  return transform ? transform->GetLocalScale() : glm::vec3(1.0f);
}
void ObjectSetScale(const GameObject& object, const glm::vec3& scale) {
  if (!SceneArg(object)) return;
  if (Transform* transform = object.GetTransform())
    transform->SetLocalScale(scale);
}
glm::vec3 ObjectGetVelocity(const GameObject& object) {
  if (!SceneArg(object)) return glm::vec3(0.0f);
  Transform* transform = object.GetTransform();
  return transform ? transform->GetVelocity() : glm::vec3(0.0f);
}
void ObjectSetVelocity(const GameObject& object, const glm::vec3& velocity) {
  if (!SceneArg(object)) return;
  if (Transform* transform = object.GetTransform())
    transform->SetVelocity(velocity);
}

// --- behaviour handles
// ---------------------------------------------------------------------

ScriptBehaviour* CallerBehaviour() {
  asIScriptContext* context = asGetActiveContext();
  if (!context) return nullptr;
  auto* object = static_cast<asIScriptObject*>(context->GetThisPointer(0));
  if (!object) return nullptr;
  return ScriptRuntime::Instance().BehaviourOf(object);
}

GameObject CallerObject() {
  ScriptBehaviour* behaviour = CallerBehaviour();
  return behaviour ? behaviour->GetGameObject() : GameObject{};
}
asIScriptObject* GetBehaviourOn(GameObject object, const std::string& name) {
  MapScene* scene = SceneArg(object);
  if (!scene) return nullptr;
  for (Behaviour* behaviour : scene->GetBehaviours(object.GetEntity())) {
    if (behaviour->GetTypeName() != name) continue;
    if (auto* script = dynamic_cast<ScriptBehaviour*>(behaviour)) {
      asIScriptObject* instance = script->Instance();
      if (instance) instance->AddRef();
      return instance;
    }
  }
  return nullptr;
}
void BehaviourSetEnabled(bool enabled) {
  if (ScriptBehaviour* behaviour = CallerBehaviour())
    behaviour->SetEnabled(enabled);
}
bool BehaviourIsEnabled() {
  ScriptBehaviour* behaviour = CallerBehaviour();
  return behaviour && behaviour->IsEnabled();
}

// --- events
// --------------------------------------------------------------------------------

void EventInvoke(ScriptEvent* self) {
  if (!self || !self->impl) {
    Throw("The Event is not bound to a script");
    return;
  }
  self->impl->Invoke();
}
void EventRemoveAllListeners(ScriptEvent* self) {
  if (self && self->impl) self->impl->RemoveAllListeners();
}

// --- scene
// ---------------------------------------------------------------------------------

CScriptArray* SceneFindObjectsWithScript(const std::string& class_name) {
  MapScene* scene = CurrentScene();
  std::vector<GameObject> objects;
  if (scene) {
    for (kEntityId entity : scene->GetRegistry().view<NodeInfo>()) {
      if (!scene->IsActiveInHierarchy(entity)) continue;
      for (Behaviour* behaviour : scene->GetBehaviours(entity)) {
        if (behaviour->GetTypeName() == class_name) {
          objects.push_back(scene->GetGameObject(entity));
          break;
        }
      }
    }
  }
  return MakeObjectArray(objects);
}
GameObject SceneFind(const std::string& path) {
  MapScene* scene = CurrentScene();
  if (!scene) return {};
  return scene->Find(path);
}
GameObject SceneFindWithTag(const std::string& tag) {
  MapScene* scene = CurrentScene();
  if (!scene) return {};
  return scene->FindWithTag(tag);
}
CScriptArray* SceneFindAllWithTag(const std::string& tag) {
  MapScene* scene = CurrentScene();
  if (!scene) return MakeObjectArray({});
  return MakeObjectArray(scene->FindGameObjectsWithTag(tag));
}
GameObject SceneInstantiate(const std::string& path, const glm::vec3& position,
                            const glm::quat& rotation, GameObject parent) {
  MapScene* scene = CurrentScene();
  if (!scene) return {};
  if (parent && !parent.IsValid()) {
    Throw("The parent was destroyed");
    return {};
  }
  GameObject created =
      scene->InstantiatePrefab(path, position, rotation, parent);
  if (!created) Throw(("Could not instantiate " + path).c_str());
  return created;
}
GameObject SceneCreateGameObject(const std::string& name, GameObject parent) {
  MapScene* scene = CurrentScene();
  if (!scene) return {};
  return scene->CreateGameObject(name, parent);
}
void SceneLoadMap(const std::string& path) {
  if (MapScene* scene = CurrentScene()) scene->LoadMap(path);
}
void SceneQuit() {
  if (MapScene* scene = CurrentScene()) scene->QuitScene();
}

// --- physics
// -------------------------------------------------------------------------------

void AddForce(GameObject object, const glm::vec3& force, int mode) {
  MapScene* scene = SceneArg(object);
  if (!scene) return;
  scene->Physics().AddForce(object.GetEntity(), force,
                            static_cast<ForceMode>(mode));
}
void AddImpulse(GameObject object, const glm::vec3& impulse) {
  MapScene* scene = SceneArg(object);
  if (!scene) return;
  scene->Physics().AddImpulse(object.GetEntity(), impulse);
}
void AddTorque(GameObject object, const glm::vec3& torque, int mode) {
  MapScene* scene = SceneArg(object);
  if (!scene) return;
  scene->Physics().AddTorque(object.GetEntity(), torque,
                             static_cast<ForceMode>(mode));
}
void SetAngularVelocity(GameObject object, const glm::vec3& velocity) {
  MapScene* scene = SceneArg(object);
  if (!scene) return;
  scene->Physics().SetAngularVelocity(object.GetEntity(), velocity);
}
bool PhysicsRaycast(const glm::vec3& origin, const glm::vec3& direction,
                    float max_distance, uint32_t mask, bool triggers,
                    GameObject* hit, glm::vec3* point, glm::vec3* normal,
                    float* distance) {
  MapScene* scene = CurrentScene();
  if (!scene) return false;
  auto result =
      scene->Physics().Raycast(origin, direction, max_distance, mask, triggers);
  if (!result) return false;
  if (hit) *hit = GameObject(scene, result->entity);
  if (point) *point = result->point;
  if (normal) *normal = result->normal;
  if (distance) *distance = result->distance;
  return true;
}

// --- input
// ---------------------------------------------------------------------------------

bool InputIsKeyPressed(int key) {
  MapScene* scene = CurrentScene();
  return scene && scene->Input() &&
         scene->Input()->IsPressed(static_cast<Key>(key));
}
bool InputIsKeyJustPressed(int key) {
  MapScene* scene = CurrentScene();
  return scene && scene->Input() &&
         scene->Input()->IsJustPressed(static_cast<Key>(key));
}
bool InputIsMouseButtonPressed(int button) {
  MapScene* scene = CurrentScene();
  return scene && scene->Input() && button >= 0 && button <= 2 &&
         scene->Input()->IsPressed(static_cast<MouseButton>(button));
}
bool InputIsPointerOverUI() {
  MapScene* scene = CurrentScene();
  return scene && scene->GetUI().IsPointerOverUI();
}
glm::vec2 InputCursorDelta() {
  MapScene* scene = CurrentScene();
  if (!scene || !scene->Input()) return glm::vec2(0.0f);
  return scene->Input()->GetCursorDelta();
}
glm::vec2 InputScroll() {
  MapScene* scene = CurrentScene();
  if (!scene || !scene->Input()) return glm::vec2(0.0f);
  return scene->Input()->GetFrameScroll();
}

ScriptInputAction FindAction(const std::string& name) {
  ScriptInputAction result;
  MapScene* scene = CurrentScene();
  InputActionAsset* asset = scene ? scene->GetInputActions() : nullptr;
  InputAction* action = asset ? asset->Find(name) : nullptr;
  if (!action) return result;
  const auto& actions = asset->GetActions();
  for (size_t i = 0; i < actions.size(); i++) {
    if (actions[i].get() == action) {
      result.index = static_cast<int>(i);
      return result;
    }
  }
  return result;
}
ScriptInputAction RequireAction(const std::string& name) {
  ScriptInputAction result = FindAction(name);
  if (result.index < 0) {
    Throw(("The project has no input action \"" + name + "\"").c_str());
  }
  return result;
}
InputAction* ActionAt(const ScriptInputAction& action) {
  MapScene* scene = CurrentScene();
  InputActionAsset* asset = scene ? scene->GetInputActions() : nullptr;
  if (!asset || action.index < 0 ||
      static_cast<size_t>(action.index) >= asset->GetActions().size()) {
    Throw("The input action no longer exists");
    return nullptr;
  }
  return asset->GetActions()[action.index].get();
}
float ActionReadFloat(const ScriptInputAction& action) {
  InputAction* item = ActionAt(action);
  return item ? item->ReadValue<float>() : 0.0f;
}
glm::vec2 ActionReadVector2(ScriptInputAction action) {
  InputAction* item = ActionAt(action);
  return item ? item->ReadValue<glm::vec2>() : glm::vec2(0.0f);
}
bool ActionIsPressed(const ScriptInputAction& action) {
  InputAction* item = ActionAt(action);
  return item && item->IsPressed();
}
bool ActionWasPressed(const ScriptInputAction& action) {
  InputAction* item = ActionAt(action);
  return item && item->WasPressedThisFrame();
}
bool ActionWasReleased(const ScriptInputAction& action) {
  InputAction* item = ActionAt(action);
  return item && item->WasReleasedThisFrame();
}
void ActionEnable(const ScriptInputAction& action, bool enable) {
  if (InputAction* item = ActionAt(action)) {
    if (enable)
      item->Enable();
    else
      item->Disable();
  }
}
void EnableMap(const std::string& map, bool enable) {
  MapScene* scene = CurrentScene();
  InputActionAsset* asset = scene ? scene->GetInputActions() : nullptr;
  if (!asset) return;
  if (enable)
    asset->EnableMap(map);
  else
    asset->DisableMap(map);
}

// --- time, random, math, debug
// -------------------------------------------------------------

float TimeDelta() { return Time::GetDeltaTime(); }
float TimeFixedDelta() { return Time::GetFixedDeltaTime(); }
double TimeNow() { return Time::GetTime(); }
int TimeFrame() { return Time::GetFrameCount(); }
float TimeScale() { return Time::GetTimeScale(); }
void SetTimeScale(float scale) { Time::SetTimeScale(scale); }

float RandomValue() {
  return std::uniform_real_distribution<float>(0.0f, 1.0f)(Rng());
}
float RandomRange(float min, float max) {
  return std::uniform_real_distribution<float>(min, max)(Rng());
}
int RandomRangeInt(int min, int max) {
  return std::uniform_int_distribution<int>(min, max)(Rng());
}
void RandomSeed(int seed) { Rng().seed(static_cast<uint32_t>(seed)); }
glm::vec2 RandomInsideUnitCircle() {
  const float angle = RandomRange(0.0f, 6.2831853f);
  const float radius = std::sqrt(RandomValue());
  return glm::vec2(std::cos(angle) * radius, std::sin(angle) * radius);
}

float Clamp(float value, float min, float max) {
  return std::clamp(value, min, max);
}
float Lerp(float a, float b, float t) {
  return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}
float Abs(float value) { return std::abs(value); }
float Sqrt(float value) { return std::sqrt(value); }
float Sin(float value) { return std::sin(value); }
float Cos(float value) { return std::cos(value); }
float Sign(float value) {
  return value < 0.0f ? -1.0f : (value > 0.0f ? 1.0f : 0.0f);
}
float Min(float a, float b) { return std::min(a, b); }
float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
float Floor(float value) { return std::floor(value); }
float Pow(float base, float exponent) { return std::pow(base, exponent); }
float Max(float a, float b) { return std::max(a, b); }
float Repeat(float value, float length) { return Mathf::Repeat(value, length); }
float Deg2Rad() { return Mathf::kDeg2Rad; }
float Rad2Deg() { return Mathf::kRad2Deg; }

void Log(const std::string& text) { PDebug::log("{}", text); }
void LogInfo(const std::string& text) { PDebug::info("{}", text); }
void LogWarn(const std::string& text) { PDebug::warn("{}", text); }
void LogError(const std::string& text) { PDebug::error("{}", text); }

// --- data assets
// ---------------------------------------------------------------------------

asIScriptObject* LoadAsset(const std::string& path,
                           const std::string& class_name) {
  MapScene* scene = CurrentScene();
  if (!scene) {
    Throw("Data assets need a running scene");
    return nullptr;
  }
  const ScriptClass* script_class = Runtime().FindScriptClass(class_name);
  if (!script_class || !script_class->data_asset) {
    Throw((class_name + " is not a [dataAsset] class").c_str());
    return nullptr;
  }
  auto asset = scene->Assets().Load(path);
  if (!asset) {
    Throw(("Could not load " + path).c_str());
    return nullptr;
  }
  auto holder = ScriptBehaviour::Create(*script_class, nullptr, entt::null);
  if (!holder) return nullptr;
  auto reflector = Reflector::Read(asset->properties);
  holder->Reflect(reflector);
  LogReflectionWarnings(reflector, asset->path);
  asIScriptObject* instance = holder->Instance();
  if (instance) instance->AddRef();
  Runtime().OwnDataAsset(std::move(holder));
  return instance;
}

int KeyFromName(const std::string& name) {
  static const std::pair<const char*, int> values[] = {{"unknown", -1},
                                                       {"space", 32},
                                                       {"apostrophe", 39},
                                                       {"comma", 44},
                                                       {"minus", 45},
                                                       {"period", 46},
                                                       {"slash", 47},
                                                       {"num0", 48},
                                                       {"num1", 49},
                                                       {"num2", 50},
                                                       {"num3", 51},
                                                       {"num4", 52},
                                                       {"num5", 53},
                                                       {"num6", 54},
                                                       {"num7", 55},
                                                       {"num8", 56},
                                                       {"num9", 57},
                                                       {"semicolon", 59},
                                                       {"equal", 61},
                                                       {"a", 65},
                                                       {"b", 66},
                                                       {"c", 67},
                                                       {"d", 68},
                                                       {"e", 69},
                                                       {"f", 70},
                                                       {"g", 71},
                                                       {"h", 72},
                                                       {"i", 73},
                                                       {"j", 74},
                                                       {"k", 75},
                                                       {"l", 76},
                                                       {"m", 77},
                                                       {"n", 78},
                                                       {"o", 79},
                                                       {"p", 80},
                                                       {"q", 81},
                                                       {"r", 82},
                                                       {"s", 83},
                                                       {"t", 84},
                                                       {"u", 85},
                                                       {"v", 86},
                                                       {"w", 87},
                                                       {"x", 88},
                                                       {"y", 89},
                                                       {"z", 90},
                                                       {"escape", 256},
                                                       {"enter", 257},
                                                       {"tab", 258},
                                                       {"backspace", 259},
                                                       {"insert", 260},
                                                       {"delete", 261},
                                                       {"right", 262},
                                                       {"left", 263},
                                                       {"down", 264},
                                                       {"up", 265},
                                                       {"pageup", 266},
                                                       {"pagedown", 267},
                                                       {"home", 268},
                                                       {"end", 269},
                                                       {"f1", 290},
                                                       {"f2", 291},
                                                       {"f3", 292},
                                                       {"f4", 293},
                                                       {"f5", 294},
                                                       {"f6", 295},
                                                       {"f7", 296},
                                                       {"f8", 297},
                                                       {"f9", 298},
                                                       {"f10", 299},
                                                       {"f11", 300},
                                                       {"f12", 301},
                                                       {"leftshift", 340},
                                                       {"leftcontrol", 341},
                                                       {"leftalt", 342},
                                                       {"rightshift", 344},
                                                       {"rightcontrol", 345},
                                                       {"rightalt", 346}};
  std::string lower;
  lower.reserve(name.size());
  for (char c : name)
    lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  for (const auto& [key, value] : values) {
    if (lower == key) return value;
  }
  return -1;
}
// --- registration
// --------------------------------------------------------------------------

void RegisterKeyEnum(asIScriptEngine* engine) {
  engine->RegisterEnum("Key");
  static const std::pair<const char*, int> keys[] = {{"Unknown", -1},
                                                     {"Space", 32},
                                                     {"Apostrophe", 39},
                                                     {"Comma", 44},
                                                     {"Minus", 45},
                                                     {"Period", 46},
                                                     {"Slash", 47},
                                                     {"Num0", 48},
                                                     {"Num1", 49},
                                                     {"Num2", 50},
                                                     {"Num3", 51},
                                                     {"Num4", 52},
                                                     {"Num5", 53},
                                                     {"Num6", 54},
                                                     {"Num7", 55},
                                                     {"Num8", 56},
                                                     {"Num9", 57},
                                                     {"Semicolon", 59},
                                                     {"Equal", 61},
                                                     {"A", 65},
                                                     {"B", 66},
                                                     {"C", 67},
                                                     {"D", 68},
                                                     {"E", 69},
                                                     {"F", 70},
                                                     {"G", 71},
                                                     {"H", 72},
                                                     {"I", 73},
                                                     {"J", 74},
                                                     {"K", 75},
                                                     {"L", 76},
                                                     {"M", 77},
                                                     {"N", 78},
                                                     {"O", 79},
                                                     {"P", 80},
                                                     {"Q", 81},
                                                     {"R", 82},
                                                     {"S", 83},
                                                     {"T", 84},
                                                     {"U", 85},
                                                     {"V", 86},
                                                     {"W", 87},
                                                     {"X", 88},
                                                     {"Y", 89},
                                                     {"Z", 90},
                                                     {"Escape", 256},
                                                     {"Enter", 257},
                                                     {"Tab", 258},
                                                     {"Backspace", 259},
                                                     {"Insert", 260},
                                                     {"Delete", 261},
                                                     {"Right", 262},
                                                     {"Left", 263},
                                                     {"Down", 264},
                                                     {"Up", 265},
                                                     {"PageUp", 266},
                                                     {"PageDown", 267},
                                                     {"Home", 268},
                                                     {"End", 269},
                                                     {"F1", 290},
                                                     {"F2", 291},
                                                     {"F3", 292},
                                                     {"F4", 293},
                                                     {"F5", 294},
                                                     {"F6", 295},
                                                     {"F7", 296},
                                                     {"F8", 297},
                                                     {"F9", 298},
                                                     {"F10", 299},
                                                     {"F11", 300},
                                                     {"F12", 301},
                                                     {"LeftShift", 340},
                                                     {"LeftControl", 341},
                                                     {"LeftAlt", 342},
                                                     {"RightShift", 344},
                                                     {"RightControl", 345},
                                                     {"RightAlt", 346}};
  for (const auto& [name, value] : keys)
    engine->RegisterEnumValue("Key", name, value);

  engine->RegisterEnum("MouseButton");
  engine->RegisterEnumValue("MouseButton", "Left", 0);
  engine->RegisterEnumValue("MouseButton", "Right", 1);
  engine->RegisterEnumValue("MouseButton", "Middle", 2);

  engine->RegisterEnum("ForceMode");
  engine->RegisterEnumValue("ForceMode", "Force",
                            static_cast<int>(ForceMode::kForce));
  engine->RegisterEnumValue("ForceMode", "Impulse",
                            static_cast<int>(ForceMode::kImpulse));
  engine->RegisterEnumValue("ForceMode", "VelocityChange",
                            static_cast<int>(ForceMode::kVelocityChange));
  engine->RegisterEnumValue("ForceMode", "Acceleration",
                            static_cast<int>(ForceMode::kAcceleration));
}

void RegisterValueTypes(asIScriptEngine* engine) {
  RegisterValueType<glm::vec2>(engine, "Vector2", asOBJ_APP_CLASS_ALLFLOATS);
  engine->RegisterObjectBehaviour(
      "Vector2", asBEHAVE_CONSTRUCT, "void f(float, float)",
      asFUNCTION(ConstructVector2), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "float get_x() const property",
                               asFUNCTION(Vec2X), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "void set_x(float) property",
                               asFUNCTION(SetVec2X), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "float get_y() const property",
                               asFUNCTION(Vec2Y), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "void set_y(float) property",
                               asFUNCTION(SetVec2Y), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "float Length() const",
                               asFUNCTION(Vec2Length), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "float SqrLength() const",
                               asFUNCTION(Vec2SqrLength),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "Vector2 Normalized() const",
                               asFUNCTION(Vec2Normalized),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector2", "float Dot(const Vector2 &in) const",
                               asFUNCTION(Vec2Dot), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector2", "Vector2 opAdd(const Vector2 &in) const",
      asFUNCTION(+[](const glm::vec2& a, const glm::vec2& b) { return a + b; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector2", "Vector2 opSub(const Vector2 &in) const",
      asFUNCTION(+[](const glm::vec2& a, const glm::vec2& b) { return a - b; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector2", "Vector2 opMul(float) const",
      asFUNCTION(+[](const glm::vec2& a, float s) { return a * s; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector2", "Vector2 opDiv(float) const",
      asFUNCTION(+[](const glm::vec2& a, float s) { return a / s; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector2", "bool opEquals(const Vector2 &in) const",
      asFUNCTION(
          +[](const glm::vec2& a, const glm::vec2& b) { return a == b; }),
      asCALL_CDECL_OBJFIRST);

  RegisterValueType<glm::vec3>(engine, "Vector3", asOBJ_APP_CLASS_ALLFLOATS);
  engine->RegisterObjectBehaviour(
      "Vector3", asBEHAVE_CONSTRUCT, "void f(float, float, float)",
      asFUNCTION(ConstructVector3), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float get_x() const property",
                               asFUNCTION(Vec3X), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "void set_x(float) property",
                               asFUNCTION(SetVec3X), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float get_y() const property",
                               asFUNCTION(Vec3Y), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "void set_y(float) property",
                               asFUNCTION(SetVec3Y), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float get_z() const property",
                               asFUNCTION(Vec3Z), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "void set_z(float) property",
                               asFUNCTION(SetVec3Z), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float Length() const",
                               asFUNCTION(Vec3Length), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float SqrLength() const",
                               asFUNCTION(Vec3SqrLength),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "Vector3 Normalized() const",
                               asFUNCTION(Vec3Normalized),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "float Dot(const Vector3 &in) const",
                               asFUNCTION(Vec3Dot), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3",
                               "Vector3 Cross(const Vector3 &in) const",
                               asFUNCTION(Vec3Cross), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Vector3", "Vector3 Scale(float) const",
                               asFUNCTION(Vec3Scale), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "Vector3 opAdd(const Vector3 &in) const",
      asFUNCTION(+[](const glm::vec3& a, const glm::vec3& b) { return a + b; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "Vector3 opSub(const Vector3 &in) const",
      asFUNCTION(+[](const glm::vec3& a, const glm::vec3& b) { return a - b; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "Vector3 opMul(float) const",
      asFUNCTION(+[](const glm::vec3& a, float s) { return a * s; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "Vector3 opDiv(float) const",
      asFUNCTION(+[](const glm::vec3& a, float s) { return a / s; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "Vector3 opNeg() const",
      asFUNCTION(+[](const glm::vec3& a) { return -a; }),
      asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Vector3", "bool opEquals(const Vector3 &in) const",
      asFUNCTION(
          +[](const glm::vec3& a, const glm::vec3& b) { return a == b; }),
      asCALL_CDECL_OBJFIRST);

  RegisterValueType<glm::quat>(engine, "Quaternion", asOBJ_APP_CLASS_ALLFLOATS);
  engine->RegisterObjectBehaviour(
      "Quaternion", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)",
      asFUNCTION(ConstructQuaternion), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion", "float get_x() const property",
                               asFUNCTION(QuatX), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion", "float get_y() const property",
                               asFUNCTION(QuatY), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion", "float get_z() const property",
                               asFUNCTION(QuatZ), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion", "float get_w() const property",
                               asFUNCTION(QuatW), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion",
                               "Quaternion Mul(const Quaternion &in) const",
                               asFUNCTION(QuatMul), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion", "Quaternion Normalized() const",
                               asFUNCTION(QuatNormalized),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Quaternion",
                               "Vector3 Rotate(const Vector3 &in) const",
                               asFUNCTION(QuatRotate), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "Quaternion", "Quaternion Slerp(const Quaternion &in, float) const",
      asFUNCTION(QuatSlerp), asCALL_CDECL_OBJFIRST);
  engine->RegisterGlobalFunction(
      "Quaternion QuaternionEuler(float, float, float)", asFUNCTION(QuatEuler),
      asCALL_CDECL);

  RegisterValueType<glm::vec4>(engine, "Color", asOBJ_APP_CLASS_ALLFLOATS);
  engine->RegisterObjectBehaviour(
      "Color", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)",
      asFUNCTION(ConstructColor), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "float get_r() const property",
                               asFUNCTION(ColorR), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "float get_g() const property",
                               asFUNCTION(ColorG), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "float get_b() const property",
                               asFUNCTION(ColorB), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "float get_a() const property",
                               asFUNCTION(ColorA), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "void set_r(float) property",
                               asFUNCTION(SetColorR), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "void set_g(float) property",
                               asFUNCTION(SetColorG), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "void set_b(float) property",
                               asFUNCTION(SetColorB), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Color", "void set_a(float) property",
                               asFUNCTION(SetColorA), asCALL_CDECL_OBJFIRST);

  RegisterValueType<GameObject>(engine, "GameObject", asOBJ_APP_CLASS_ALLINTS);
  engine->RegisterObjectMethod("GameObject", "string get_name() const property",
                               asFUNCTION(ObjectGetName),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "string get_path() const property",
                               asFUNCTION(ObjectGetPath),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "bool get_isValid() const property",
      asFUNCTION(ObjectIsValid), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "string get_tag() const property",
                               asFUNCTION(ObjectGetTag), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject",
                               "void set_tag(const string &in) property",
                               asFUNCTION(ObjectSetTag), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "int get_layer() const property",
                               asFUNCTION(ObjectGetLayer),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "void set_layer(int) property",
                               asFUNCTION(ObjectSetLayer),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "bool get_activeSelf() const property",
      asFUNCTION(ObjectActiveSelf), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "bool get_activeInHierarchy() const property",
      asFUNCTION(ObjectActiveInHierarchy), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "void set_active(bool) property",
                               asFUNCTION(ObjectSetActive),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "GameObject get_parent() const property",
      asFUNCTION(ObjectGetParent), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "array<GameObject>@ get_children() const property",
      asFUNCTION(ObjectGetChildren), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "GameObject FindChild(const string &in) const",
      asFUNCTION(ObjectFindChild), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "void SetParent(GameObject, bool)",
                               asFUNCTION(ObjectSetParent),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("GameObject", "void Destroy()",
                               asFUNCTION(ObjectDestroy),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_position() const property",
      asFUNCTION(ObjectGetPosition), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "void set_position(const Vector3 &in) property",
      asFUNCTION(ObjectSetPosition), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Quaternion get_rotation() const property",
      asFUNCTION(ObjectGetRotation), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "void set_rotation(const Quaternion &in) property",
      asFUNCTION(ObjectSetRotation), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_eulerAngles() const property",
      asFUNCTION(ObjectGetEuler), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "void set_eulerAngles(const Vector3 &in) property",
      asFUNCTION(ObjectSetEuler), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_localScale() const property",
      asFUNCTION(ObjectGetScale), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "void set_localScale(const Vector3 &in) property",
      asFUNCTION(ObjectSetScale), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_velocity() const property",
      asFUNCTION(ObjectGetVelocity), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "bool get_hasBounds() const property",
      asFUNCTION(ObjectHasBounds), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_boundsMin() const property",
      asFUNCTION(ObjectBoundsMin), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "Vector3 get_boundsMax() const property",
      asFUNCTION(ObjectBoundsMax), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "GameObject", "void set_velocity(const Vector3 &in) property",
      asFUNCTION(ObjectSetVelocity), asCALL_CDECL_OBJFIRST);

  RegisterValueType<ScriptEvent>(engine, "Event", asOBJ_APP_CLASS_ALLINTS);
  engine->RegisterObjectMethod("Event", "void Invoke()",
                               asFUNCTION(EventInvoke), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("Event", "void RemoveAllListeners()",
                               asFUNCTION(EventRemoveAllListeners),
                               asCALL_CDECL_OBJFIRST);

  RegisterValueType<ScriptInputAction>(engine, "InputAction",
                                       asOBJ_APP_CLASS_ALLINTS);
  engine->RegisterObjectMethod("InputAction", "float ReadFloat() const",
                               asFUNCTION(ActionReadFloat),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("InputAction", "Vector2 ReadVector2() const",
                               asFUNCTION(ActionReadVector2),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "InputAction", "bool get_isPressed() const property",
      asFUNCTION(ActionIsPressed), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "InputAction", "bool get_wasPressedThisFrame() const property",
      asFUNCTION(ActionWasPressed), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "InputAction", "bool get_wasReleasedThisFrame() const property",
      asFUNCTION(ActionWasReleased), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("InputAction", "void Enable()",
                               asFUNCTION(+[](const ScriptInputAction& action) {
                                 ActionEnable(action, true);
                               }),
                               asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("InputAction", "void Disable()",
                               asFUNCTION(+[](const ScriptInputAction& action) {
                                 ActionEnable(action, false);
                               }),
                               asCALL_CDECL_OBJFIRST);
}

}  // namespace

MapScene* SceneArg(GameObject object) {
  if (!object.IsValid()) {
    Throw("GameObject was destroyed");
    return nullptr;
  }
  return object.GetScene();
}

void InstallAll(asIScriptEngine* engine) {
  InstallCore(engine);
  InstallComponents(engine);
  InstallUi(engine);
}

void InstallCore(asIScriptEngine* engine) {
  RegisterKeyEnum(engine);
  RegisterValueTypes(engine);

  engine->RegisterGlobalFunction("GameObject GetGameObject()",
                                 asFUNCTION(CallerObject), asCALL_CDECL);
  engine->RegisterGlobalFunction("void SetEnabled(bool)",
                                 asFUNCTION(BehaviourSetEnabled), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool IsEnabled()",
                                 asFUNCTION(BehaviourIsEnabled), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "IBehaviour@ GetBehaviour(GameObject, const string &in)",
      asFUNCTION(GetBehaviourOn), asCALL_CDECL);
  engine->RegisterGlobalFunction("IBehaviour@ GetBehaviour(const string &in)",
                                 asFUNCTION(+[](const std::string& name) {
                                   return GetBehaviourOn(CallerObject(), name);
                                 }),
                                 asCALL_CDECL);

  engine->SetDefaultNamespace("Scene");
  engine->RegisterGlobalFunction("GameObject Find(const string &in)",
                                 asFUNCTION(SceneFind), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "array<GameObject>@ FindObjectsWithScript(const string &in)",
      asFUNCTION(SceneFindObjectsWithScript), asCALL_CDECL);
  engine->RegisterGlobalFunction("GameObject FindWithTag(const string &in)",
                                 asFUNCTION(SceneFindWithTag), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "array<GameObject>@ FindAllWithTag(const string &in)",
      asFUNCTION(SceneFindAllWithTag), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "GameObject Instantiate(const string &in, const Vector3 &in, const "
      "Quaternion &in, GameObject)",
      asFUNCTION(SceneInstantiate), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "GameObject CreateGameObject(const string &in, GameObject)",
      asFUNCTION(SceneCreateGameObject), asCALL_CDECL);
  engine->RegisterGlobalFunction("void LoadMap(const string &in)",
                                 asFUNCTION(SceneLoadMap), asCALL_CDECL);
  engine->RegisterGlobalFunction("void Quit()", asFUNCTION(SceneQuit),
                                 asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Physics");
  engine->RegisterGlobalFunction(
      "void AddForce(GameObject, const Vector3 &in, int)", asFUNCTION(AddForce),
      asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AddImpulse(GameObject, const Vector3 &in)", asFUNCTION(AddImpulse),
      asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void AddTorque(GameObject, const Vector3 &in, int)",
      asFUNCTION(AddTorque), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "void SetAngularVelocity(GameObject, const Vector3 &in)",
      asFUNCTION(SetAngularVelocity), asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "bool Raycast(const Vector3 &in, const Vector3 &in, float, uint, bool, "
      "GameObject &out, "
      "Vector3 &out, Vector3 &out, float &out)",
      asFUNCTION(PhysicsRaycast), asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Input");
  engine->RegisterGlobalFunction("int KeyFromName(const string &in)",
                                 asFUNCTION(KeyFromName), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool IsKeyPressed(int)",
                                 asFUNCTION(InputIsKeyPressed), asCALL_CDECL);
  engine->RegisterGlobalFunction("bool IsKeyJustPressed(int)",
                                 asFUNCTION(InputIsKeyJustPressed),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("bool IsMouseButtonPressed(int)",
                                 asFUNCTION(InputIsMouseButtonPressed),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction(
      "bool IsPointerOverUI()", asFUNCTION(InputIsPointerOverUI), asCALL_CDECL);
  engine->RegisterGlobalFunction("Vector2 CursorDelta()",
                                 asFUNCTION(InputCursorDelta), asCALL_CDECL);
  engine->RegisterGlobalFunction("Vector2 Scroll()", asFUNCTION(InputScroll),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("InputAction FindAction(const string &in)",
                                 asFUNCTION(FindAction), asCALL_CDECL);
  engine->RegisterGlobalFunction("InputAction RequireAction(const string &in)",
                                 asFUNCTION(RequireAction), asCALL_CDECL);
  engine->RegisterGlobalFunction("void EnableMap(const string &in, bool)",
                                 asFUNCTION(EnableMap), asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Time");
  engine->RegisterGlobalFunction("float DeltaTime()", asFUNCTION(TimeDelta),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float FixedDeltaTime()",
                                 asFUNCTION(TimeFixedDelta), asCALL_CDECL);
  engine->RegisterGlobalFunction("double Now()", asFUNCTION(TimeNow),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("int FrameCount()", asFUNCTION(TimeFrame),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float TimeScale()", asFUNCTION(TimeScale),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("void SetTimeScale(float)",
                                 asFUNCTION(SetTimeScale), asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Random");
  engine->RegisterGlobalFunction("float Value()", asFUNCTION(RandomValue),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Range(float, float)",
                                 asFUNCTION(RandomRange), asCALL_CDECL);
  engine->RegisterGlobalFunction("int RangeInt(int, int)",
                                 asFUNCTION(RandomRangeInt), asCALL_CDECL);
  engine->RegisterGlobalFunction("void Seed(int)", asFUNCTION(RandomSeed),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("Vector2 InsideUnitCircle()",
                                 asFUNCTION(RandomInsideUnitCircle),
                                 asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Mathf");
  engine->RegisterGlobalFunction("float Clamp(float, float, float)",
                                 asFUNCTION(Clamp), asCALL_CDECL);
  engine->RegisterGlobalFunction("float Lerp(float, float, float)",
                                 asFUNCTION(Lerp), asCALL_CDECL);
  engine->RegisterGlobalFunction("float Abs(float)", asFUNCTION(Abs),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Sqrt(float)", asFUNCTION(Sqrt),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Sin(float)", asFUNCTION(Sin),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Cos(float)", asFUNCTION(Cos),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Sign(float)", asFUNCTION(Sign),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Min(float, float)", asFUNCTION(Min),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Clamp01(float)", asFUNCTION(Clamp01),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Floor(float)", asFUNCTION(Floor),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Pow(float, float)", asFUNCTION(Pow),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Max(float, float)", asFUNCTION(Max),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Repeat(float, float)",
                                 asFUNCTION(Repeat), asCALL_CDECL);
  engine->RegisterGlobalFunction("float Deg2Rad()", asFUNCTION(Deg2Rad),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("float Rad2Deg()", asFUNCTION(Rad2Deg),
                                 asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Debug");
  engine->RegisterGlobalFunction("void Log(const string &in)", asFUNCTION(Log),
                                 asCALL_CDECL);
  engine->RegisterGlobalFunction("void Info(const string &in)",
                                 asFUNCTION(LogInfo), asCALL_CDECL);
  engine->RegisterGlobalFunction("void Warn(const string &in)",
                                 asFUNCTION(LogWarn), asCALL_CDECL);
  engine->RegisterGlobalFunction("void Error(const string &in)",
                                 asFUNCTION(LogError), asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->SetDefaultNamespace("Assets");
  engine->RegisterGlobalFunction(
      "IDataAsset@ Load(const string &in, const string &in)",
      asFUNCTION(LoadAsset), asCALL_CDECL);
  engine->SetDefaultNamespace("");
}

}  // namespace Mercury::as
