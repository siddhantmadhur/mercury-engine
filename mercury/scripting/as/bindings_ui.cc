// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/*
 * bindings_ui.cc
 *
 * Natives for UI Toolkit elements. A script holds a ScriptUiHandle (an
 * integer), never an element pointer. The table is emptied whenever a
 * UIDocument is destroyed, after which old handles throw.
 */
#include "scripting/as/bindings_ui.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "scripting/as/bindings.h"
#include "scripting/as/script_behaviour.h"
#include "scripting/as/script_runtime.h"
#include "ui/controls.h"
#include "ui/panel.h"
#include "ui/ui_document.h"

namespace Mercury {

namespace {

// One live UI element. The weak token is the element's own alive_ flag, so a
// handle to an element that was removed (not only a whole document) is
// detected instead of dereferencing freed memory. Slots are keyed by a
// never-reused id, so a handle cannot silently name a different element.
struct ElementSlot {
  UI::VisualElement* element = nullptr;
  std::weak_ptr<const bool> alive;
};

std::unordered_map<uint32_t, ElementSlot> g_elements;
uint32_t g_next_slot = 1;
uint32_t g_generation = 1;

bool SlotAlive(const ElementSlot& slot) {
  const std::shared_ptr<const bool> alive = slot.alive.lock();
  return alive && *alive;
}

int64_t EncodeHandle(uint32_t slot) {
  return static_cast<int64_t>((uint64_t{g_generation} << 32) | slot);
}

}  // namespace

void InvalidateUiHandles() {
  g_elements.clear();
  g_next_slot = 1;
  // Generation 0 is never handed out; skip it if the counter wraps.
  if (++g_generation == 0) g_generation = 1;
}

namespace as {

namespace {

int64_t MakeHandle(UI::VisualElement* element) {
  if (!element) return -1;
  for (auto it = g_elements.begin(); it != g_elements.end();) {
    // Drop the rows of elements that have been destroyed, so the table stays
    // as small as the live elements.
    if (!SlotAlive(it->second)) {
      it = g_elements.erase(it);
      continue;
    }
    if (it->second.element == element) return EncodeHandle(it->first);
    ++it;
  }
  if (g_next_slot == 0) {
    // The 32-bit slot id wrapped; start a new generation so no live handle
    // can match a new slot.
    g_elements.clear();
    if (++g_generation == 0) g_generation = 1;
    g_next_slot = 1;
  }
  const uint32_t slot = g_next_slot++;
  g_elements.emplace(slot, ElementSlot{element, element->aliveToken()});
  return EncodeHandle(slot);
}

UI::VisualElement* ArgElement(const ScriptUiHandle& handle) {
  const uint64_t raw = static_cast<uint64_t>(handle.value);
  const uint32_t generation = static_cast<uint32_t>(raw >> 32);
  const auto it = g_elements.find(static_cast<uint32_t>(raw));
  if (generation != g_generation || it == g_elements.end() ||
      !SlotAlive(it->second)) {
    Throw("UI element was destroyed");
    return nullptr;
  }
  return it->second.element;
}

// Keeps the script object that registered a UI listener alive while the
// listener exists, and releases it when the element is destroyed.
struct ScriptOwner {
  explicit ScriptOwner(asIScriptObject* instance) : object(instance) {
    if (object) object->AddRef();
  }
  ~ScriptOwner() {
    if (object) object->Release();
  }
  asIScriptObject* object = nullptr;
};

// Listeners stay on the element after their behaviour is destroyed, so they
// must not call into it.
void CallOwner(asIScriptObject* owner, const std::string& method,
               std::span<const ScriptArg> args, std::string_view label) {
  if (!owner || !ScriptRuntime::Instance().BehaviourOf(owner)) return;
  ScriptRuntime::Instance().CallMethod(owner, method, args, label);
}

ScriptUiHandle Root() {
  asIScriptContext* context = asGetActiveContext();
  auto* object = context
                     ? static_cast<asIScriptObject*>(context->GetThisPointer(0))
                     : nullptr;
  ScriptBehaviour* behaviour =
      object ? ScriptRuntime::Instance().BehaviourOf(object) : nullptr;
  if (!behaviour) return {};
  auto* document = behaviour->GetComponent<UIDocument>();
  return ScriptUiHandle{document ? MakeHandle(document->rootVisualElement())
                                 : -1};
}

ScriptUiHandle Query(const ScriptUiHandle& handle, const std::string& name,
                     const std::string& class_name) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return {};
  return ScriptUiHandle{
      MakeHandle(element->Q<UI::VisualElement>(name, class_name))};
}

ScriptUiHandle QueryByName(const ScriptUiHandle& handle,
                           const std::string& name) {
  return Query(handle, name, "");
}

ScriptUiHandle QueryByClass(const ScriptUiHandle& handle,
                            const std::string& class_name) {
  return Query(handle, "", class_name);
}

void SetText(const ScriptUiHandle& handle, const std::string& text) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  if (auto* label = dynamic_cast<UI::TextElement*>(element)) {
    label->SetText(text);
  } else if (auto* toggle = dynamic_cast<UI::Toggle*>(element)) {
    toggle->SetText(text);
  } else {
    Throw("This UI element has no text");
  }
}

std::string GetText(const ScriptUiHandle& handle) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return {};
  if (auto* label = dynamic_cast<UI::TextElement*>(element))
    return label->text();
  return {};
}

void OnClick(const ScriptUiHandle& handle, const std::string& method) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  auto* button = dynamic_cast<UI::Button*>(element);
  if (!button) {
    Throw("Only a Button has clicked");
    return;
  }
  asIScriptContext* context = asGetActiveContext();
  auto* owner = context
                    ? static_cast<asIScriptObject*>(context->GetThisPointer(0))
                    : nullptr;
  if (!owner) {
    Throw("OnClick must be called from a script method");
    return;
  }
  auto holder = std::make_shared<ScriptOwner>(owner);
  button->clicked += [holder, method] {
    CallOwner(holder->object, method, {}, "Button." + method);
  };
}

void OnClickArg(const ScriptUiHandle& handle, const std::string& method,
                const std::string& argument) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  auto* button = dynamic_cast<UI::Button*>(element);
  if (!button) {
    Throw("Only a Button has clicked");
    return;
  }
  asIScriptContext* context = asGetActiveContext();
  auto* owner = context
                    ? static_cast<asIScriptObject*>(context->GetThisPointer(0))
                    : nullptr;
  if (!owner) {
    Throw("OnClick must be called from a script method");
    return;
  }
  auto holder = std::make_shared<ScriptOwner>(owner);
  button->clicked += [holder, method, argument] {
    const ScriptArg args[] = {ScriptArg::String(argument)};
    CallOwner(holder->object, method, args, "Button." + method);
  };
}

void OnToggleChanged(const ScriptUiHandle& handle, const std::string& method) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  auto* toggle = dynamic_cast<UI::Toggle*>(element);
  if (!toggle) {
    Throw("Only a Toggle has a value");
    return;
  }
  asIScriptContext* context = asGetActiveContext();
  auto* owner = context
                    ? static_cast<asIScriptObject*>(context->GetThisPointer(0))
                    : nullptr;
  if (!owner) {
    Throw("OnToggleChanged must be called from a script method");
    return;
  }
  auto holder = std::make_shared<ScriptOwner>(owner);
  toggle->RegisterValueChangedCallback<bool>(
      [holder, method](UI::ChangeEvent<bool>& event) {
        const ScriptArg args[] = {ScriptArg::Bool(event.newValue)};
        CallOwner(holder->object, method, args, "Toggle." + method);
      });
}

bool ToggleValue(const ScriptUiHandle& handle) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return false;
  auto* toggle = dynamic_cast<UI::Toggle*>(element);
  if (!toggle) {
    Throw("Only a Toggle has a value");
    return false;
  }
  return toggle->value();
}

void SetToggleValue(const ScriptUiHandle& handle, bool value) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  auto* toggle = dynamic_cast<UI::Toggle*>(element);
  if (!toggle) {
    Throw("Only a Toggle has a value");
    return;
  }
  toggle->SetValue(value);
}

void SetClass(const ScriptUiHandle& handle, const std::string& name,
              bool enable) {
  if (UI::VisualElement* element = ArgElement(handle))
    element->EnableInClassList(name, enable);
}

bool HasClass(const ScriptUiHandle& handle, const std::string& name) {
  UI::VisualElement* element = ArgElement(handle);
  return element && element->ClassListContains(name);
}

void Focus(const ScriptUiHandle& handle) {
  if (UI::VisualElement* element = ArgElement(handle)) element->Focus();
}

void BlurFocused(const ScriptUiHandle& handle) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element || !element->panel()) return;
  if (auto* focused = element->panel()->focusedElement()) focused->Blur();
}

void SetDisplay(const ScriptUiHandle& handle, bool visible) {
  UI::VisualElement* element = ArgElement(handle);
  if (!element) return;
  element->style().display =
      visible ? UI::DisplayStyle::kFlex : UI::DisplayStyle::kNone;
}

bool IsValid(const ScriptUiHandle& handle) {
  const uint64_t raw = static_cast<uint64_t>(handle.value);
  const uint32_t generation = static_cast<uint32_t>(raw >> 32);
  const auto it = g_elements.find(static_cast<uint32_t>(raw));
  return generation == g_generation && it != g_elements.end() &&
         SlotAlive(it->second);
}

void SetDataSource(const ScriptUiHandle& handle) {
  UI::VisualElement* element = ArgElement(handle);
  asIScriptContext* context = asGetActiveContext();
  auto* object = context
                     ? static_cast<asIScriptObject*>(context->GetThisPointer(0))
                     : nullptr;
  ScriptBehaviour* behaviour =
      object ? ScriptRuntime::Instance().BehaviourOf(object) : nullptr;
  if (!element || !behaviour) return;
  element->SetDataSource(behaviour->AsDataSource());
}

}  // namespace

void InstallUi(asIScriptEngine* engine) {
  RegisterValueType<ScriptUiHandle>(engine, "VisualElement",
                                    asOBJ_APP_CLASS_ALLINTS);

  engine->SetDefaultNamespace("UI");
  engine->RegisterGlobalFunction("VisualElement Root()", asFUNCTION(Root),
                                 asCALL_CDECL);
  engine->SetDefaultNamespace("");

  engine->RegisterObjectMethod(
      "VisualElement", "VisualElement Q(const string &in, const string &in)",
      asFUNCTION(Query), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "VisualElement Q(const string &in)",
                               asFUNCTION(QueryByName), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "VisualElement QClass(const string &in)",
                               asFUNCTION(QueryByClass), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "void SetText(const string &in)",
                               asFUNCTION(SetText), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement", "string GetText() const",
                               asFUNCTION(GetText), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "string get_text() const property",
                               asFUNCTION(GetText), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "void set_text(const string &in) property",
                               asFUNCTION(SetText), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "void OnClick(const string &in)",
                               asFUNCTION(OnClick), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "VisualElement", "void OnClick(const string &in, const string &in)",
      asFUNCTION(OnClickArg), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "VisualElement", "void OnToggleChanged(const string &in)",
      asFUNCTION(OnToggleChanged), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "bool get_toggleValue() const property",
                               asFUNCTION(ToggleValue), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod(
      "VisualElement", "void set_toggleValue(bool) property",
      asFUNCTION(SetToggleValue), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "void SetClass(const string &in, bool)",
                               asFUNCTION(SetClass), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "bool HasClass(const string &in) const",
                               asFUNCTION(HasClass), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement", "void Focus()",
                               asFUNCTION(Focus), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement", "void BlurFocused()",
                               asFUNCTION(BlurFocused), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement", "void SetDisplay(bool)",
                               asFUNCTION(SetDisplay), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement",
                               "bool get_isValid() const property",
                               asFUNCTION(IsValid), asCALL_CDECL_OBJFIRST);
  engine->RegisterObjectMethod("VisualElement", "void SetDataSource()",
                               asFUNCTION(SetDataSource),
                               asCALL_CDECL_OBJFIRST);
}

}  // namespace as
}  // namespace Mercury
