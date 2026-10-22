/*
 * visual_element.h
 *
 * VisualElement, the building block of every UI, after Unity UI Toolkit.
 * Elements form a tree owned by their parents; a Panel lays the tree out
 * with Yoga (flexbox), styles it from USS and draws it.
 *
 *   auto* box = root->Add(std::make_unique<VisualElement>());
 *   box->AddToClassList("hud");
 *   box->style().flexDirection = FlexDirection::kRow;
 *   auto* label = box->Emplace<Label>("Score: 0");
 *   root->Q<Label>("score")->SetText("12");
 */
#ifndef MERCURY_UI_VISUAL_ELEMENT_H
#define MERCURY_UI_VISUAL_ELEMENT_H

#include <any>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "serialization/mtext.h"
#include "ui/events.h"
#include "ui/style.h"
#include "ui/style_sheet.h"

struct YGNode;

namespace Mercury::UI {

class Panel;
class IDataSource;
struct DataBinding;
class Manipulator;
class MeshGenerationContext;
template <typename T>
class UQueryBuilder;

enum class PickingMode : uint8_t { kPosition, kIgnore };

using CallbackId = uint64_t;

enum class MeasureMode : uint8_t { kUndefined, kExactly, kAtMost };

/**
 * @brief A delayed or repeating action (element->schedule().Execute(...))
 */
class ScheduledItem {
 public:
  // Waits this long before the first run.
  ScheduledItem& StartingIn(double ms) {
    delay_ms_ = ms;
    return *this;
  }
  // Runs again every `ms` until paused or until Until() returns true.
  ScheduledItem& Every(double ms) {
    interval_ms_ = ms;
    repeat_ = true;
    return *this;
  }
  ScheduledItem& Until(std::function<bool()> stop) {
    until_ = std::move(stop);
    return *this;
  }
  ScheduledItem& ForDuration(double ms) {
    duration_ms_ = ms;
    repeat_ = true;
    return *this;
  }
  void Pause() { paused_ = true; }
  void Resume() { paused_ = false; }
  // Stops it for good; it is removed on the next update.
  void Cancel() { done_ = true; }
  bool isActive() const { return !paused_ && !done_; }

 private:
  friend class VisualElement;
  std::function<void()> action_;
  std::function<bool()> until_;
  double delay_ms_ = 0.0, interval_ms_ = 0.0, duration_ms_ = -1.0;
  double started_at_ = -1.0, next_run_ = 0.0;
  bool repeat_ = false, paused_ = false, done_ = false;
};

class VisualElement {
 public:
  VisualElement();
  virtual ~VisualElement();
  VisualElement(const VisualElement&) = delete;
  VisualElement& operator=(const VisualElement&) = delete;

  // Name used by USS type selectors and UXML tags.
  virtual const char* typeName() const { return "VisualElement"; }

  // ------------------------------------------------------------ identity --
  const std::string& name() const { return name_; }
  void SetName(std::string name);

  const std::vector<std::string>& GetClasses() const { return classes_; }
  void AddToClassList(std::string_view class_name);
  void RemoveFromClassList(std::string_view class_name);
  void ClearClassList();
  void EnableInClassList(std::string_view class_name, bool enable);
  void ToggleInClassList(std::string_view class_name);
  bool ClassListContains(std::string_view class_name) const;

  // ----------------------------------------------------------- hierarchy --
  // The logical parent: for children of a ScrollView this is the ScrollView
  // content container, as in Unity.
  VisualElement* parent() const { return parent_; }
  Panel* panel() const { return panel_; }
  // Where Add() puts children; composite controls override it.
  virtual VisualElement* contentContainer() { return this; }

  // Adds to contentContainer(); returns the child.
  VisualElement* Add(std::unique_ptr<VisualElement> child);
  template <typename T>
  T* Add(std::unique_ptr<T> child) {
    T* raw = child.get();
    Add(std::unique_ptr<VisualElement>(std::move(child)));
    return raw;
  }
  // Constructs a child in place: box->Emplace<Label>("text").
  template <typename T = VisualElement, typename... Args>
  T* Emplace(Args&&... args) {
    return Add(std::make_unique<T>(std::forward<Args>(args)...));
  }
  VisualElement* Insert(int index, std::unique_ptr<VisualElement> child);
  std::unique_ptr<VisualElement> Remove(VisualElement* child);
  std::unique_ptr<VisualElement> RemoveAt(int index);
  void Clear();
  // Detaches this element from its parent and hands ownership back.
  std::unique_ptr<VisualElement> RemoveFromHierarchy();
  void BringToFront();
  void SendToBack();

  int childCount() const;
  VisualElement* ElementAt(int index) const;
  int IndexOf(const VisualElement* child) const;
  std::vector<VisualElement*> Children() const;
  // True if `element` is this element or one of its descendants.
  bool Contains(const VisualElement* element) const;

  // The physical children, ignoring contentContainer (Unity's hierarchy).
  struct Hierarchy {
    VisualElement* owner;
    VisualElement* parent() const { return owner->hierarchy_parent_; }
    VisualElement* Add(std::unique_ptr<VisualElement> child);
    VisualElement* Insert(int index, std::unique_ptr<VisualElement> child);
    std::unique_ptr<VisualElement> Remove(VisualElement* child);
    int childCount() const;
    VisualElement* ElementAt(int index) const;
    int IndexOf(const VisualElement* child) const;
  };
  Hierarchy hierarchy() { return {this}; }
  const std::vector<std::unique_ptr<VisualElement>>& hierarchyChildren() const {
    return children_;
  }
  VisualElement* hierarchyParent() const { return hierarchy_parent_; }

  // First descendant (or this) matching name, class and type T.
  template <typename T = VisualElement>
  T* Q(std::string_view name = {}, std::string_view class_name = {});
  template <typename T = VisualElement>
  UQueryBuilder<T> Query(std::string_view name = {},
                         std::string_view class_name = {});

  // --------------------------------------------------------------- state --
  bool enabledSelf() const { return enabled_self_; }
  bool enabledInHierarchy() const;
  void SetEnabled(bool enabled);

  // Shortcut for style().visibility.
  bool visible() const;
  void SetVisible(bool visible);

  PickingMode pickingMode() const { return picking_mode_; }
  void SetPickingMode(PickingMode mode) { picking_mode_ = mode; }

  bool focusable() const { return focusable_; }
  void SetFocusable(bool focusable) { focusable_ = focusable; }
  int tabIndex() const { return tab_index_; }
  void SetTabIndex(int index) { tab_index_ = index; }
  // Focus the first focusable child instead of this element.
  bool delegatesFocus() const { return delegates_focus_; }
  void SetDelegatesFocus(bool delegates) { delegates_focus_ = delegates; }
  virtual bool canGrabFocus() const;
  void Focus();
  void Blur();
  bool IsFocused() const;

  const std::string& tooltip() const { return tooltip_; }
  void SetTooltip(std::string tooltip) { tooltip_ = std::move(tooltip); }

  // Anything the game wants to keep on the element.
  std::any userData;
  // Opaque pointer back to the UXML node this element was cloned from
  // (used by the UI Builder).
  const void* uxmlSource = nullptr;

  uint32_t pseudoStates() const { return pseudo_states_; }
  void SetPseudoState(uint32_t state, bool on);

  // ------------------------------------------------------------- styling --
  // Values set here win over style sheets. Non-const access marks the
  // element for restyling.
  InlineStyle& style();
  const InlineStyle& style() const { return inline_style_; }
  // The values in use after the cascade and any running transitions.
  const ComputedStyle& resolvedStyle() const { return resolved_; }
  // The values from the cascade, before transitions.
  const ComputedStyle& computedStyle() const { return computed_; }

  // Style sheets that apply to this element and its descendants.
  void AddStyleSheet(std::shared_ptr<StyleSheet> sheet);
  void RemoveStyleSheet(const StyleSheet* sheet);
  void ClearStyleSheets();
  const std::vector<std::shared_ptr<StyleSheet>>& styleSheets() const {
    return style_sheets_;
  }

  // Custom properties (--name) after the cascade, with var()s resolved.
  const std::unordered_map<std::string, TokenList>& customStyle() const {
    return custom_properties_;
  }
  bool TryGetCustomStyle(std::string_view name, float& out) const;
  bool TryGetCustomStyle(std::string_view name, Color& out) const;
  bool TryGetCustomStyle(std::string_view name, std::string& out) const;

  void MarkStyleDirty();
  void MarkDirtyRepaint();
  void MarkLayoutDirty();

  // -------------------------------------------------------------- layout --
  // Position and size relative to the parent's top-left corner.
  const Rect& layout() const { return layout_; }
  // Axis-aligned bounds in panel coordinates, after transforms.
  Rect worldBound() const;
  Rect localBound() const;
  // Inside the border and padding, in local coordinates.
  Rect contentRect() const;
  Rect paddingRect() const;
  // Maps local coordinates to panel coordinates.
  glm::mat3 worldTransform() const;
  // Maps local coordinates to the parent's (layout position + transform).
  glm::mat3 localTransform() const;
  glm::vec2 WorldToLocal(glm::vec2 world) const;
  glm::vec2 LocalToWorld(glm::vec2 local) const;
  virtual bool ContainsPoint(glm::vec2 local) const;

  // Offsets the children (used by ScrollView).
  glm::vec2 childrenOffset() const { return children_offset_; }
  void SetChildrenOffset(glm::vec2 offset);

  // -------------------------------------------------------------- events --
  template <typename E>
  CallbackId RegisterCallback(std::function<void(E&)> callback,
                              TrickleDown phase = TrickleDown::kNoTrickleDown) {
    return AddCallback(
        E::TypeId(),
        [callback = std::move(callback)](EventBase& e) {
          callback(static_cast<E&>(e));
        },
        phase);
  }
  template <typename T>
  CallbackId RegisterValueChangedCallback(
      std::function<void(ChangeEvent<T>&)> callback) {
    return RegisterCallback<ChangeEvent<T>>(std::move(callback));
  }
  void UnregisterCallback(CallbackId id);
  // Removes every callback of one event type.
  template <typename E>
  void UnregisterCallbacks() {
    RemoveCallbacksOfType(E::TypeId());
  }

  // Sends an event through the panel (or just to this element if it is not
  // in a panel).
  void SendEvent(EventBase& event);

  Manipulator* AddManipulator(std::unique_ptr<Manipulator> manipulator);
  void RemoveManipulator(Manipulator* manipulator);

  void CapturePointer(int pointer_id = 0);
  void ReleasePointer(int pointer_id = 0);
  bool HasPointerCapture(int pointer_id = 0) const;

  // ---------------------------------------------------------- scheduling --
  ScheduledItem& Schedule(std::function<void()> action);

  // ---------------------------------------------------------- data binding --
  // Inherited by children that have none.
  void SetDataSource(std::shared_ptr<IDataSource> source);
  const std::shared_ptr<IDataSource>& dataSource() const { return data_source_; }
  // The source in use: this element's, else the nearest ancestor's.
  std::shared_ptr<IDataSource> ResolveDataSource() const;
  // Prefix for the binding paths of this element and its children.
  const std::string& dataSourcePath() const { return data_source_path_; }
  void SetDataSourcePath(std::string path);
  std::string ResolveDataSourcePath() const;

  void SetBinding(std::string_view property, DataBinding binding);
  void ClearBinding(std::string_view property);
  void ClearBindings();
  DataBinding* GetBinding(std::string_view property);
  const std::vector<std::pair<std::string, std::unique_ptr<DataBinding>>>&
  bindings() const {
    return bindings_;
  }

  // Named properties, used by bindings and UXML attributes ("text",
  // "value", "name", ...). Subclasses add theirs and call the base.
  virtual bool SetProperty(std::string_view name, const mtext::Variant& value);
  virtual std::optional<mtext::Variant> GetProperty(std::string_view name) const;

  // ------------------------------------------------------------- drawing --
  // Custom drawing after the background and border (Unity's
  // generateVisualContent).
  std::function<void(MeshGenerationContext&)> generateVisualContent;

  // -------------------------------------------------- used by the system --
  // Becomes false when the element is destroyed; lets dispatch code notice
  // a callback that deleted its own element.
  std::shared_ptr<const bool> aliveToken() const { return alive_; }
  YGNode* yogaNode() const { return yoga_node_; }
  virtual bool HasMeasure() const { return false; }
  virtual glm::vec2 Measure(float width, MeasureMode width_mode, float height,
                            MeasureMode height_mode);
  // Draws content other than background/border (text, images...).
  virtual void GenerateVisualContent(MeshGenerationContext& context) {}
  // Control behavior, run after the element's own callbacks for each phase.
  virtual void HandleEventTrickleDown(EventBase& event) {}
  virtual void HandleEventBubbleUp(EventBase& event) {}

  // Runs this element's callbacks registered for `phase`.
  void InvokeCallbacks(EventBase& event, TrickleDown phase);
  void SetPanel(Panel* panel);
  void SetLayout(const Rect& rect) { layout_ = rect; }
  bool styleDirty() const { return style_dirty_; }
  bool subtreeStyleDirty() const { return subtree_style_dirty_; }
  void ClearStyleDirty() {
    style_dirty_ = false;
    subtree_style_dirty_ = false;
  }
  void SetComputedStyle(const ComputedStyle& computed, bool animate);
  void SetCustomProperties(std::unordered_map<std::string, TokenList> props) {
    custom_properties_ = std::move(props);
  }
  // Advances transitions; returns true while any are running.
  bool UpdateTransitions(double now);
  bool HasRunningTransitions() const { return !transitions_.empty(); }
  void RunScheduledItems(double now_ms);
  void ApplyStyleToYoga();
  void UpdateMeasureFunc();
  void UpdateBindings();

 protected:
  // Called when the text or anything else that changes the measured size
  // changes.
  void MarkMeasureDirty();
  // Subclasses call this when a property changes so bindings and the UI
  // Builder see the new value (and the element is redrawn).
  void NotifyPropertyChanged() { MarkDirtyRepaint(); }

 private:
  friend class Panel;
  struct Callback {
    CallbackId id;
    EventTypeId type;
    TrickleDown phase;
    std::function<void(EventBase&)> fn;
    bool removed = false;
  };
  struct Transition {
    StylePropertyId property;
    StyleValue from, to;
    double start = -1.0;  // set on the first update
    double delay = 0.0, duration = 0.0;
    EasingMode easing = EasingMode::kEase;
  };

  CallbackId AddCallback(EventTypeId type, std::function<void(EventBase&)> fn,
                         TrickleDown phase);
  void RemoveCallbacksOfType(EventTypeId type);
  VisualElement* HierarchyInsert(int index, std::unique_ptr<VisualElement> child);
  std::unique_ptr<VisualElement> HierarchyRemove(VisualElement* child);
  void MarkSubtreeStyleDirty();

  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  std::string name_;
  std::vector<std::string> classes_;
  VisualElement* parent_ = nullptr;            // logical parent
  VisualElement* hierarchy_parent_ = nullptr;  // physical parent
  std::vector<std::unique_ptr<VisualElement>> children_;
  Panel* panel_ = nullptr;
  YGNode* yoga_node_ = nullptr;
  bool has_measure_func_ = false;

  bool enabled_self_ = true;
  PickingMode picking_mode_ = PickingMode::kPosition;
  bool focusable_ = false;
  bool delegates_focus_ = false;
  int tab_index_ = 0;
  std::string tooltip_;
  uint32_t pseudo_states_ = 0;

  InlineStyle inline_style_;
  ComputedStyle computed_;
  ComputedStyle resolved_;
  bool has_been_styled_ = false;
  std::vector<Transition> transitions_;
  std::vector<std::shared_ptr<StyleSheet>> style_sheets_;
  std::unordered_map<std::string, TokenList> custom_properties_;
  bool style_dirty_ = true;
  bool subtree_style_dirty_ = true;

  Rect layout_;
  glm::vec2 children_offset_{0.0f};

  std::vector<Callback> callbacks_;
  int dispatch_depth_ = 0;
  std::vector<std::unique_ptr<Manipulator>> manipulators_;
  std::vector<std::unique_ptr<ScheduledItem>> scheduled_;

  std::shared_ptr<IDataSource> data_source_;
  std::string data_source_path_;
  std::vector<std::pair<std::string, std::unique_ptr<DataBinding>>> bindings_;
};

/**
 * @brief Adds behavior to an element by listening to its events
 */
class Manipulator {
 public:
  virtual ~Manipulator() = default;
  VisualElement* target() const { return target_; }
  void SetTarget(VisualElement* target);

 protected:
  virtual void RegisterCallbacksOnTarget() = 0;
  virtual void UnregisterCallbacksFromTarget() = 0;

 private:
  VisualElement* target_ = nullptr;
};

/**
 * @brief Turns presses into clicks: fires `clicked` when the pointer is
 * pressed and released over the target, and sets :active while held
 */
class Clickable : public Manipulator {
 public:
  explicit Clickable(std::function<void()> handler = {},
                     double delay_ms = 0.0, double interval_ms = 0.0);

  std::function<void()> clicked;
  // Same as clicked, with the triggering event.
  std::function<void(EventBase&)> clickedWithEventInfo;
  bool active() const { return active_; }

 protected:
  void RegisterCallbacksOnTarget() override;
  void UnregisterCallbacksFromTarget() override;

 private:
  void Invoke(EventBase& event);
  double delay_ms_, interval_ms_;
  bool active_ = false;
  ScheduledItem* repeater_ = nullptr;
  std::vector<CallbackId> callbacks_;
};

// Type name match used by Q<T>: true if `element` is a T.
template <typename T>
bool IsElementOfType(const VisualElement* element) {
  return dynamic_cast<const T*>(element) != nullptr;
}

}  // namespace Mercury::UI

#include "ui/query.h"

#endif  // MERCURY_UI_VISUAL_ELEMENT_H
