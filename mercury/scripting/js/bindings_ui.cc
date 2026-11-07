/*
 * bindings_ui.cc
 *
 * Natives for UI Toolkit elements. A script never holds an element pointer: it holds a handle
 * (an integer) from the table below. The table is emptied whenever a UIDocument is destroyed
 * (and so when a scene goes away), after which old handles throw "UI element was destroyed".
 * Listeners keep their JS function in a v8::Global owned by the C++ callback, so it is freed
 * together with the element.
 */
#include "scripting/js/bindings.h"
#include "scripting/js/script_behaviour.h"
#include "ui/controls.h"
#include "ui/ui_document.h"

namespace Mercury {

namespace {
std::vector<UI::VisualElement*> g_elements;
uint32_t g_generation = 1;
}  // namespace

void InvalidateUiHandles() {
  g_elements.clear();
  g_generation++;
}

namespace js {

namespace {

double MakeHandle(UI::VisualElement* element) {
  if (!element) return -1.0;
  for (size_t i = 0; i < g_elements.size(); i++) {
    if (g_elements[i] == element) return static_cast<double>((uint64_t{g_generation} << 20) | i);
  }
  g_elements.push_back(element);
  return static_cast<double>((uint64_t{g_generation} << 20) | (g_elements.size() - 1));
}

UI::VisualElement* ArgElement(const Info& info, int i) {
  double raw;
  if (!ArgNumber(info, i, &raw)) return nullptr;
  const uint64_t handle = static_cast<uint64_t>(raw);
  const uint32_t generation = static_cast<uint32_t>(handle >> 20);
  const size_t index = static_cast<size_t>(handle & 0xFFFFF);
  if (generation != g_generation || index >= g_elements.size()) {
    Throw(info.GetIsolate(), "UI element was destroyed");
    return nullptr;
  }
  return g_elements[index];
}

// A JS function kept alive by the C++ callback that calls it.
struct JsCallback {
  v8::Global<v8::Function> function;
  std::string label;
  uint32_t scene = ScriptRuntime::Instance().CurrentScene();
  void Invoke(std::span<v8::Local<v8::Value>> args) const {
    auto& runtime = ScriptRuntime::Instance();
    if (!runtime.IsInitialized()) return;
    v8::Isolate::Scope isolate_scope(runtime.GetIsolate());
    v8::HandleScope handle_scope(runtime.GetIsolate());
    v8::Local<v8::Context> context = runtime.GetContext();
    v8::Context::Scope context_scope(context);
    runtime.SetCurrentScene(scene);
    runtime.Call(function.Get(runtime.GetIsolate()), context->Global(), args, label);
  }
};

std::shared_ptr<JsCallback> MakeCallback(const Info& info, int i, std::string label) {
  if (i >= info.Length() || !info[i]->IsFunction()) {
    ThrowType(info.GetIsolate(), "argument " + std::to_string(i) + " must be a function");
    return nullptr;
  }
  auto callback = std::make_shared<JsCallback>();
  callback->function.Reset(info.GetIsolate(), info[i].As<v8::Function>());
  callback->label = std::move(label);
  return callback;
}

// uiRoot(scene, entity) -> handle or -1
void UiRoot(const Info& info) {
  auto o = ArgGameObject(info, 0);
  if (!o) return;
  auto* document = o->GetComponent<UIDocument>();
  info.GetReturnValue().Set(document ? MakeHandle(document->rootVisualElement()) : -1.0);
}

// uiQuery(handle, name, className) -> handle or -1
void UiQuery(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  std::string name, class_name;
  if (!element || !ArgString(info, 1, &name) || !ArgString(info, 2, &class_name)) return;
  info.GetReturnValue().Set(MakeHandle(element->Q<UI::VisualElement>(name, class_name)));
}

void UiSetText(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  std::string text;
  if (!element || !ArgString(info, 1, &text)) return;
  if (auto* label = dynamic_cast<UI::TextElement*>(element)) {
    label->SetText(text);
  } else if (auto* toggle = dynamic_cast<UI::Toggle*>(element)) {
    toggle->SetText(text);
  } else {
    Throw(info.GetIsolate(), "This UI element has no text");
  }
}

void UiGetText(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  if (!element) return;
  if (auto* label = dynamic_cast<UI::TextElement*>(element)) {
    info.GetReturnValue().Set(Str(info.GetIsolate(), label->text()));
  } else {
    info.GetReturnValue().Set(Str(info.GetIsolate(), ""));
  }
}

void UiOnClick(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  if (!element) return;
  auto* button = dynamic_cast<UI::Button*>(element);
  if (!button) {
    Throw(info.GetIsolate(), "Only a Button has clicked");
    return;
  }
  auto callback = MakeCallback(info, 1, "Button.clicked");
  if (!callback) return;
  button->clicked += [callback] { callback->Invoke({}); };
}

void UiToggleValue(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  if (!element) return;
  auto* toggle = dynamic_cast<UI::Toggle*>(element);
  if (!toggle) {
    Throw(info.GetIsolate(), "Only a Toggle has a value");
    return;
  }
  if (info.Length() > 1 && info[1]->IsBoolean()) {
    toggle->SetValue(info[1].As<v8::Boolean>()->Value());
  }
  info.GetReturnValue().Set(toggle->value());
}

void UiOnToggleChanged(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  if (!element) return;
  auto* toggle = dynamic_cast<UI::Toggle*>(element);
  if (!toggle) {
    Throw(info.GetIsolate(), "Only a Toggle has a value");
    return;
  }
  auto callback = MakeCallback(info, 1, "Toggle.valueChanged");
  if (!callback) return;
  toggle->RegisterValueChangedCallback<bool>([callback](UI::ChangeEvent<bool>& e) {
    v8::Isolate* isolate = ScriptRuntime::Instance().GetIsolate();
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Value> args[] = {v8::Boolean::New(isolate, e.newValue)};
    callback->Invoke(args);
  });
}

void UiClass(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  std::string name;
  bool enable;
  if (!element || !ArgString(info, 1, &name) || !ArgBool(info, 2, &enable)) return;
  element->EnableInClassList(name, enable);
}

void UiFocus(const Info& info) {
  if (UI::VisualElement* element = ArgElement(info, 0)) element->Focus();
}

// uiSetDisplay(handle, visible): style.display flex / none
void UiSetDisplay(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  bool visible;
  if (!element || !ArgBool(info, 1, &visible)) return;
  element->style().display = visible ? UI::DisplayStyle::kFlex : UI::DisplayStyle::kNone;
}

// uiSetDataSource(handle, behaviourId): the script's fields and @bindable getters become
// binding paths.
void UiSetDataSource(const Info& info) {
  UI::VisualElement* element = ArgElement(info, 0);
  int id;
  if (!element || !ArgInt(info, 1, &id)) return;
  ScriptBehaviour* behaviour = ScriptRuntime::Instance().FindBehaviour(id);
  if (!behaviour) {
    Throw(info.GetIsolate(), "The script was destroyed");
    return;
  }
  element->SetDataSource(behaviour->AsDataSource());
}

}  // namespace

void InstallUi(v8::Local<v8::Context> context, v8::Local<v8::Object> native) {
  SetFunction(context, native, "uiRoot", UiRoot);
  SetFunction(context, native, "uiQuery", UiQuery);
  SetFunction(context, native, "uiSetText", UiSetText);
  SetFunction(context, native, "uiGetText", UiGetText);
  SetFunction(context, native, "uiOnClick", UiOnClick);
  SetFunction(context, native, "uiToggleValue", UiToggleValue);
  SetFunction(context, native, "uiOnToggleChanged", UiOnToggleChanged);
  SetFunction(context, native, "uiClass", UiClass);
  SetFunction(context, native, "uiFocus", UiFocus);
  SetFunction(context, native, "uiSetDisplay", UiSetDisplay);
  SetFunction(context, native, "uiSetDataSource", UiSetDataSource);
}

void InstallCore(v8::Local<v8::Context>, v8::Local<v8::Object>);
void InstallComponents(v8::Local<v8::Context>, v8::Local<v8::Object>);

void InstallAll(v8::Local<v8::Context> context, v8::Local<v8::Object> native) {
  InstallCore(context, native);
  InstallUi(context, native);
  InstallComponents(context, native);
}

}  // namespace js
}  // namespace Mercury
