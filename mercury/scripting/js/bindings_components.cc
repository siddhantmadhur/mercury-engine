/*
 * bindings_components.cc
 *
 * Generic access to the engine's built-in components. The TypeScript classes in
 * generated/components.ts are made from the TypeRegistry, and each typed property calls these
 * with the component and field name.
 */
#include "scripting/js/bindings.h"
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

}  // namespace

void InstallComponents(v8::Local<v8::Context> context, v8::Local<v8::Object> native) {
  SetFunction(context, native, "componentHas", ComponentHas);
  SetFunction(context, native, "componentGet", ComponentGet);
  SetFunction(context, native, "componentSet", ComponentSet);
  SetFunction(context, native, "componentAdd", ComponentAdd);
}

}  // namespace Mercury::js
