#include "visual_element.h"

#include <yoga/Yoga.h>

#include <algorithm>
#include <atomic>
#include <cmath>

#include "ui/binding.h"
#include "ui/data_source.h"
#include "ui/layout.h"
#include "ui/panel.h"

namespace Mercury::UI {

namespace {

std::atomic<uint64_t> g_next_callback_id{1};
std::atomic<EventTypeId> g_next_event_type{1};

glm::mat3 Translation(glm::vec2 t) {
  glm::mat3 m(1.0f);
  m[2][0] = t.x;
  m[2][1] = t.y;
  return m;
}

glm::mat3 Rotation(float degrees) {
  const float r = glm::radians(degrees);
  const float c = std::cos(r), s = std::sin(r);
  glm::mat3 m(1.0f);
  m[0][0] = c;
  m[0][1] = s;
  m[1][0] = -s;
  m[1][1] = c;
  return m;
}

glm::mat3 Scaling(glm::vec2 s) {
  glm::mat3 m(1.0f);
  m[0][0] = s.x;
  m[1][1] = s.y;
  return m;
}

// Properties that change an element's size or position in the flex layout.
bool AffectsLayout(StylePropertyId id) {
  using P = StylePropertyId;
  return id <= P::kBorderBottomWidth || id == P::kFontSize || id == P::kFont ||
         id == P::kFontStyle || id == P::kLetterSpacing ||
         id == P::kWordSpacing || id == P::kParagraphSpacing ||
         id == P::kWhiteSpace || id == P::kDisplay;
}

}  // namespace

EventTypeId NextEventTypeId() { return g_next_event_type++; }

VisualElement::VisualElement() {
  yoga_node_ = YGNodeNewWithConfig(SharedYogaConfig());
  YGNodeSetContext(yoga_node_, this);
  ApplyStyleToYoga();
}

VisualElement::~VisualElement() {
  *alive_ = false;
  if (panel_) {
    // Destroyed while still in a panel: make sure it forgets us.
    std::function<void(VisualElement*)> forget = [&](VisualElement* e) {
      panel_->OnElementDetached(e);
      for (auto& child : e->children_) forget(child.get());
    };
    forget(this);
  }
  for (auto& manipulator : manipulators_) manipulator->SetTarget(nullptr);
  manipulators_.clear();
  children_.clear();
  YGNodeFree(yoga_node_);
}

// ---------------------------------------------------------------- identity --

void VisualElement::SetName(std::string name) {
  if (name_ == name) return;
  name_ = std::move(name);
  MarkStyleDirty();
}

void VisualElement::AddToClassList(std::string_view class_name) {
  if (class_name.empty() || ClassListContains(class_name)) return;
  classes_.emplace_back(class_name);
  MarkStyleDirty();
}

void VisualElement::RemoveFromClassList(std::string_view class_name) {
  auto it = std::find(classes_.begin(), classes_.end(), class_name);
  if (it == classes_.end()) return;
  classes_.erase(it);
  MarkStyleDirty();
}

void VisualElement::ClearClassList() {
  if (classes_.empty()) return;
  classes_.clear();
  MarkStyleDirty();
}

void VisualElement::EnableInClassList(std::string_view class_name, bool enable) {
  if (enable) {
    AddToClassList(class_name);
  } else {
    RemoveFromClassList(class_name);
  }
}

void VisualElement::ToggleInClassList(std::string_view class_name) {
  EnableInClassList(class_name, !ClassListContains(class_name));
}

bool VisualElement::ClassListContains(std::string_view class_name) const {
  return std::find(classes_.begin(), classes_.end(), class_name) != classes_.end();
}

// --------------------------------------------------------------- hierarchy --

VisualElement* VisualElement::Add(std::unique_ptr<VisualElement> child) {
  VisualElement* container = contentContainer();
  VisualElement* raw = container->HierarchyInsert(
      static_cast<int>(container->children_.size()), std::move(child));
  if (raw) raw->parent_ = this;
  return raw;
}

VisualElement* VisualElement::Insert(int index, std::unique_ptr<VisualElement> child) {
  VisualElement* raw = contentContainer()->HierarchyInsert(index, std::move(child));
  if (raw) raw->parent_ = this;
  return raw;
}

std::unique_ptr<VisualElement> VisualElement::Remove(VisualElement* child) {
  return contentContainer()->HierarchyRemove(child);
}

std::unique_ptr<VisualElement> VisualElement::RemoveAt(int index) {
  VisualElement* container = contentContainer();
  if (index < 0 || index >= static_cast<int>(container->children_.size())) {
    return nullptr;
  }
  return container->HierarchyRemove(container->children_[index].get());
}

void VisualElement::Clear() {
  VisualElement* container = contentContainer();
  while (!container->children_.empty()) {
    container->HierarchyRemove(container->children_.back().get());
  }
}

std::unique_ptr<VisualElement> VisualElement::RemoveFromHierarchy() {
  if (!hierarchy_parent_) return nullptr;
  return hierarchy_parent_->HierarchyRemove(this);
}

void VisualElement::BringToFront() {
  VisualElement* p = hierarchy_parent_;
  if (!p || p->children_.back().get() == this) return;
  auto it = std::find_if(p->children_.begin(), p->children_.end(),
                         [this](const auto& c) { return c.get() == this; });
  std::unique_ptr<VisualElement> self = std::move(*it);
  p->children_.erase(it);
  YGNodeRemoveChild(p->yoga_node_, yoga_node_);
  YGNodeInsertChild(p->yoga_node_, yoga_node_, p->children_.size());
  p->children_.push_back(std::move(self));
  p->MarkLayoutDirty();
  p->MarkDirtyRepaint();
}

void VisualElement::SendToBack() {
  VisualElement* p = hierarchy_parent_;
  if (!p || p->children_.front().get() == this) return;
  auto it = std::find_if(p->children_.begin(), p->children_.end(),
                         [this](const auto& c) { return c.get() == this; });
  std::unique_ptr<VisualElement> self = std::move(*it);
  p->children_.erase(it);
  YGNodeRemoveChild(p->yoga_node_, yoga_node_);
  YGNodeInsertChild(p->yoga_node_, yoga_node_, 0);
  p->children_.insert(p->children_.begin(), std::move(self));
  p->MarkLayoutDirty();
  p->MarkDirtyRepaint();
}

int VisualElement::childCount() const {
  return const_cast<VisualElement*>(this)->contentContainer()->hierarchy().childCount();
}

VisualElement* VisualElement::ElementAt(int index) const {
  return const_cast<VisualElement*>(this)->contentContainer()->hierarchy().ElementAt(index);
}

int VisualElement::IndexOf(const VisualElement* child) const {
  return const_cast<VisualElement*>(this)->contentContainer()->hierarchy().IndexOf(child);
}

std::vector<VisualElement*> VisualElement::Children() const {
  std::vector<VisualElement*> out;
  for (const auto& child :
       const_cast<VisualElement*>(this)->contentContainer()->children_) {
    out.push_back(child.get());
  }
  return out;
}

bool VisualElement::Contains(const VisualElement* element) const {
  for (const VisualElement* e = element; e; e = e->hierarchy_parent_) {
    if (e == this) return true;
  }
  return false;
}

VisualElement* VisualElement::Hierarchy::Add(std::unique_ptr<VisualElement> child) {
  return owner->HierarchyInsert(static_cast<int>(owner->children_.size()),
                                std::move(child));
}
VisualElement* VisualElement::Hierarchy::Insert(int index,
                                                std::unique_ptr<VisualElement> child) {
  return owner->HierarchyInsert(index, std::move(child));
}
std::unique_ptr<VisualElement> VisualElement::Hierarchy::Remove(VisualElement* child) {
  return owner->HierarchyRemove(child);
}
int VisualElement::Hierarchy::childCount() const {
  return static_cast<int>(owner->children_.size());
}
VisualElement* VisualElement::Hierarchy::ElementAt(int index) const {
  if (index < 0 || index >= childCount()) return nullptr;
  return owner->children_[index].get();
}
int VisualElement::Hierarchy::IndexOf(const VisualElement* child) const {
  for (size_t i = 0; i < owner->children_.size(); i++) {
    if (owner->children_[i].get() == child) return static_cast<int>(i);
  }
  return -1;
}

VisualElement* VisualElement::HierarchyInsert(int index,
                                              std::unique_ptr<VisualElement> child) {
  if (!child) return nullptr;
  index = std::clamp(index, 0, static_cast<int>(children_.size()));
  if (has_measure_func_) {
    // Yoga doesn't allow measured nodes to have children.
    YGNodeSetMeasureFunc(yoga_node_, nullptr);
    has_measure_func_ = false;
  }
  VisualElement* raw = child.get();
  YGNodeInsertChild(yoga_node_, raw->yoga_node_, static_cast<size_t>(index));
  children_.insert(children_.begin() + index, std::move(child));
  raw->hierarchy_parent_ = this;
  raw->parent_ = this;
  raw->MarkStyleDirty();
  if (panel_) raw->SetPanel(panel_);
  raw->SetEnabled(raw->enabled_self_);
  MarkLayoutDirty();
  MarkDirtyRepaint();
  return raw;
}

std::unique_ptr<VisualElement> VisualElement::HierarchyRemove(VisualElement* child) {
  auto it = std::find_if(children_.begin(), children_.end(),
                         [child](const auto& c) { return c.get() == child; });
  if (it == children_.end()) return nullptr;
  if (panel_) child->SetPanel(nullptr);
  YGNodeRemoveChild(yoga_node_, child->yoga_node_);
  std::unique_ptr<VisualElement> owned = std::move(*it);
  children_.erase(it);
  owned->hierarchy_parent_ = nullptr;
  owned->parent_ = nullptr;
  owned->SetEnabled(owned->enabled_self_);
  UpdateMeasureFunc();
  MarkLayoutDirty();
  MarkDirtyRepaint();
  return owned;
}

void VisualElement::SetPanel(Panel* panel) {
  if (panel_ == panel) return;
  if (panel_) {
    for (auto& child : children_) child->SetPanel(nullptr);
    panel_->OnElementDetached(this);
    DetachFromPanelEvent event;
    SendEvent(event);
    panel_ = nullptr;
  }
  panel_ = panel;
  if (panel_) {
    UpdateMeasureFunc();
    MarkStyleDirty();
    AttachToPanelEvent event;
    SendEvent(event);
    for (auto& child : children_) child->SetPanel(panel);
  }
}

// ------------------------------------------------------------------- state --

bool VisualElement::enabledInHierarchy() const {
  for (const VisualElement* e = this; e; e = e->hierarchy_parent_) {
    if (!e->enabled_self_) return false;
  }
  return true;
}

void VisualElement::SetEnabled(bool enabled) {
  enabled_self_ = enabled;
  std::function<void(VisualElement*)> update = [&](VisualElement* e) {
    e->SetPseudoState(kPseudoDisabled, !e->enabledInHierarchy());
    for (auto& child : e->children_) update(child.get());
  };
  update(this);
  if (!enabledInHierarchy() && IsFocused()) Blur();
}

bool VisualElement::visible() const {
  return resolved_.visibility == Visibility::kVisible;
}

void VisualElement::SetVisible(bool visible) {
  style().visibility = visible ? Visibility::kVisible : Visibility::kHidden;
}

bool VisualElement::canGrabFocus() const {
  if (!focusable_ || !panel_ || !enabledInHierarchy()) return false;
  for (const VisualElement* e = this; e; e = e->hierarchy_parent_) {
    if (e->resolved_.display == DisplayStyle::kNone) return false;
  }
  return visible();
}

void VisualElement::Focus() {
  if (!panel_) return;
  VisualElement* target = this;
  if (delegates_focus_) {
    target = nullptr;
    std::function<VisualElement*(VisualElement*)> find =
        [&](VisualElement* e) -> VisualElement* {
      for (auto& child : e->children_) {
        if (child->canGrabFocus()) return child.get();
        if (auto found = find(child.get())) return found;
      }
      return nullptr;
    };
    target = find(this);
    if (!target && canGrabFocus()) target = this;
  }
  if (target && target->canGrabFocus()) {
    panel_->focusController().SwitchFocus(target);
  }
}

void VisualElement::Blur() {
  if (IsFocused()) panel_->focusController().SwitchFocus(nullptr);
}

bool VisualElement::IsFocused() const {
  return panel_ && panel_->focusedElement() == this;
}

void VisualElement::SetPseudoState(uint32_t state, bool on) {
  const uint32_t next = on ? (pseudo_states_ | state) : (pseudo_states_ & ~state);
  if (next == pseudo_states_) return;
  pseudo_states_ = next;
  MarkStyleDirty();
}

// ----------------------------------------------------------------- styling --

InlineStyle& VisualElement::style() {
  MarkStyleDirty();
  return inline_style_;
}

void VisualElement::AddStyleSheet(std::shared_ptr<StyleSheet> sheet) {
  if (!sheet) return;
  for (const auto& s : style_sheets_) {
    if (s == sheet) return;
  }
  style_sheets_.push_back(std::move(sheet));
  MarkStyleDirty();
}

void VisualElement::RemoveStyleSheet(const StyleSheet* sheet) {
  std::erase_if(style_sheets_, [sheet](const auto& s) { return s.get() == sheet; });
  MarkStyleDirty();
}

void VisualElement::ClearStyleSheets() {
  style_sheets_.clear();
  MarkStyleDirty();
}

bool VisualElement::TryGetCustomStyle(std::string_view name, float& out) const {
  auto it = custom_properties_.find(std::string(name));
  if (it == custom_properties_.end() || it->second.empty()) return false;
  const auto& t = it->second[0];
  if (t.type == StyleToken::Type::kNumber ||
      t.type == StyleToken::Type::kDimension ||
      t.type == StyleToken::Type::kPercentage) {
    out = t.number;
    return true;
  }
  return false;
}

bool VisualElement::TryGetCustomStyle(std::string_view name, Color& out) const {
  auto it = custom_properties_.find(std::string(name));
  if (it == custom_properties_.end()) return false;
  if (auto value = ParseStyleValue(StylePropertyId::kColor, it->second)) {
    if (auto c = std::get_if<Color>(&*value)) {
      out = *c;
      return true;
    }
  }
  return false;
}

bool VisualElement::TryGetCustomStyle(std::string_view name, std::string& out) const {
  auto it = custom_properties_.find(std::string(name));
  if (it == custom_properties_.end() || it->second.empty()) return false;
  const auto& t = it->second[0];
  out = t.type == StyleToken::Type::kString ? t.text : TokensToString(it->second);
  return true;
}

void VisualElement::MarkStyleDirty() {
  style_dirty_ = true;
  subtree_style_dirty_ = true;
  for (VisualElement* p = hierarchy_parent_; p && !p->subtree_style_dirty_;
       p = p->hierarchy_parent_) {
    p->subtree_style_dirty_ = true;
  }
  if (panel_) panel_->MarkStyleDirty();
}

void VisualElement::MarkDirtyRepaint() {
  if (panel_) panel_->MarkRepaint();
}

void VisualElement::MarkLayoutDirty() {
  if (panel_) panel_->MarkLayoutDirty();
}

void VisualElement::MarkMeasureDirty() {
  if (has_measure_func_) YGNodeMarkDirty(yoga_node_);
  MarkLayoutDirty();
  MarkDirtyRepaint();
}

void VisualElement::SetComputedStyle(const ComputedStyle& computed, bool animate) {
  const ComputedStyle before = resolved_;
  if (animate && has_been_styled_) {
    for (size_t i = 0; i < kStylePropertyCount; i++) {
      const auto id = static_cast<StylePropertyId>(i);
      const StyleValue next = computed.Get(id);
      if (computed_.Get(id) == next) continue;
      std::erase_if(transitions_, [id](const Transition& t) { return t.property == id; });
      if (!GetStylePropertyInfo(id).animatable) continue;

      // Find the transition that names this property (or "all").
      const auto& names = computed.transitionProperty.names;
      const std::string_view uss = GetStylePropertyInfo(id).name;
      for (size_t n = 0; n < names.size(); n++) {
        if (names[n] != "all" && names[n] != uss) continue;
        auto pick = [n](const auto& list, auto fallback) {
          return list.empty() ? fallback : list[n % list.size()];
        };
        const float duration = pick(computed.transitionDuration.seconds, 0.0f);
        if (duration <= 0.0f) break;
        Transition t;
        t.property = id;
        t.from = resolved_.Get(id);
        t.to = next;
        t.duration = duration;
        t.delay = pick(computed.transitionDelay.seconds, 0.0f);
        t.easing = pick(computed.transitionTimingFunction.modes, EasingMode::kEase);
        transitions_.push_back(std::move(t));
        break;
      }
    }
  } else {
    transitions_.clear();
  }
  computed_ = computed;
  resolved_ = computed;
  for (const auto& t : transitions_) resolved_.Set(t.property, t.from);
  has_been_styled_ = true;

  bool layout = false;
  for (size_t i = 0; i < kStylePropertyCount && !layout; i++) {
    const auto id = static_cast<StylePropertyId>(i);
    if (AffectsLayout(id) && before.Get(id) != resolved_.Get(id)) layout = true;
  }
  if (layout) {
    ApplyStyleToYoga();
    MarkMeasureDirty();
  }
  if (!(before == resolved_)) MarkDirtyRepaint();
}

bool VisualElement::UpdateTransitions(double now) {
  if (transitions_.empty()) return false;
  bool layout = false;
  auto alive = alive_;
  for (size_t i = 0; i < transitions_.size();) {
    Transition& t = transitions_[i];
    if (t.start < 0.0) t.start = now;
    const double elapsed = now - t.start - t.delay;
    StyleValue value;
    bool finished = false;
    if (elapsed < 0.0) {
      value = t.from;
    } else if (elapsed >= t.duration) {
      value = t.to;
      finished = true;
    } else {
      const float p = Ease(t.easing, static_cast<float>(elapsed / t.duration));
      value = Interpolate(t.property, t.from, t.to, p);
    }
    resolved_.Set(t.property, value);
    if (AffectsLayout(t.property)) layout = true;
    if (finished) {
      TransitionEndEvent event;
      event.property = t.property;
      event.elapsedTime = t.duration;
      transitions_.erase(transitions_.begin() + i);
      SendEvent(event);
      if (!*alive) return false;
    } else {
      i++;
    }
  }
  if (layout) {
    ApplyStyleToYoga();
    MarkMeasureDirty();
  }
  MarkDirtyRepaint();
  return !transitions_.empty();
}

// ------------------------------------------------------------------ layout --

Rect VisualElement::localBound() const {
  // In the parent's space: transform the corners by this element's local
  // transform only.
  const glm::mat3 local = localTransform();
  glm::vec2 lo(1e30f), hi(-1e30f);
  for (glm::vec2 c : {glm::vec2(0, 0), glm::vec2(layout_.width, 0),
                      glm::vec2(0, layout_.height),
                      glm::vec2(layout_.width, layout_.height)}) {
    glm::vec3 p = local * glm::vec3(c, 1.0f);
    lo = glm::min(lo, glm::vec2(p));
    hi = glm::max(hi, glm::vec2(p));
  }
  return {lo.x, lo.y, hi.x - lo.x, hi.y - lo.y};
}

Rect VisualElement::worldBound() const {
  const glm::mat3 m = worldTransform();
  glm::vec2 lo(1e30f), hi(-1e30f);
  for (glm::vec2 c : {glm::vec2(0, 0), glm::vec2(layout_.width, 0),
                      glm::vec2(0, layout_.height),
                      glm::vec2(layout_.width, layout_.height)}) {
    glm::vec3 p = m * glm::vec3(c, 1.0f);
    lo = glm::min(lo, glm::vec2(p));
    hi = glm::max(hi, glm::vec2(p));
  }
  return {lo.x, lo.y, hi.x - lo.x, hi.y - lo.y};
}

Rect VisualElement::paddingRect() const {
  const float l = YGNodeLayoutGetBorder(yoga_node_, YGEdgeLeft);
  const float t = YGNodeLayoutGetBorder(yoga_node_, YGEdgeTop);
  const float r = YGNodeLayoutGetBorder(yoga_node_, YGEdgeRight);
  const float b = YGNodeLayoutGetBorder(yoga_node_, YGEdgeBottom);
  return {l, t, std::max(0.0f, layout_.width - l - r),
          std::max(0.0f, layout_.height - t - b)};
}

Rect VisualElement::contentRect() const {
  Rect r = paddingRect();
  const float l = YGNodeLayoutGetPadding(yoga_node_, YGEdgeLeft);
  const float t = YGNodeLayoutGetPadding(yoga_node_, YGEdgeTop);
  const float rr = YGNodeLayoutGetPadding(yoga_node_, YGEdgeRight);
  const float b = YGNodeLayoutGetPadding(yoga_node_, YGEdgeBottom);
  return {r.x + l, r.y + t, std::max(0.0f, r.width - l - rr),
          std::max(0.0f, r.height - t - b)};
}

glm::mat3 VisualElement::localTransform() const {
  const ComputedStyle& s = resolved_;
  glm::mat3 local = Translation({layout_.x, layout_.y});
  const bool transformed = s.rotate.degrees != 0.0f ||
                           s.scale.value != glm::vec2(1.0f) ||
                           s.translate.x.value != 0.0f || s.translate.y.value != 0.0f;
  if (transformed) {
    const glm::vec2 origin(s.transformOrigin.x.Resolve(layout_.width),
                           s.transformOrigin.y.Resolve(layout_.height));
    const glm::vec2 translate(s.translate.x.Resolve(layout_.width),
                              s.translate.y.Resolve(layout_.height));
    local = local * Translation(origin + translate) * Rotation(s.rotate.degrees) *
            Scaling(s.scale.value) * Translation(-origin);
  }
  return local;
}

glm::mat3 VisualElement::worldTransform() const {
  if (!hierarchy_parent_) return localTransform();
  return hierarchy_parent_->worldTransform() *
         Translation(hierarchy_parent_->children_offset_) * localTransform();
}

glm::vec2 VisualElement::WorldToLocal(glm::vec2 world) const {
  return glm::vec2(glm::inverse(worldTransform()) * glm::vec3(world, 1.0f));
}

glm::vec2 VisualElement::LocalToWorld(glm::vec2 local) const {
  return glm::vec2(worldTransform() * glm::vec3(local, 1.0f));
}

bool VisualElement::ContainsPoint(glm::vec2 local) const {
  return local.x >= 0 && local.y >= 0 && local.x < layout_.width &&
         local.y < layout_.height;
}

void VisualElement::SetChildrenOffset(glm::vec2 offset) {
  if (offset == children_offset_) return;
  children_offset_ = offset;
  MarkDirtyRepaint();
}

glm::vec2 VisualElement::Measure(float, MeasureMode, float, MeasureMode) {
  return glm::vec2(0.0f);
}

// ------------------------------------------------------------------ events --

CallbackId VisualElement::AddCallback(EventTypeId type,
                                      std::function<void(EventBase&)> fn,
                                      TrickleDown phase) {
  const CallbackId id = g_next_callback_id++;
  callbacks_.push_back({id, type, phase, std::move(fn)});
  return id;
}

void VisualElement::UnregisterCallback(CallbackId id) {
  for (auto& cb : callbacks_) {
    if (cb.id == id) cb.removed = true;
  }
  if (dispatch_depth_ == 0) {
    std::erase_if(callbacks_, [](const Callback& c) { return c.removed; });
  }
}

void VisualElement::RemoveCallbacksOfType(EventTypeId type) {
  for (auto& cb : callbacks_) {
    if (cb.type == type) cb.removed = true;
  }
  if (dispatch_depth_ == 0) {
    std::erase_if(callbacks_, [](const Callback& c) { return c.removed; });
  }
}

void VisualElement::InvokeCallbacks(EventBase& event, TrickleDown phase) {
  auto alive = alive_;
  dispatch_depth_++;
  const EventTypeId type = event.eventTypeId();
  // Callbacks added while dispatching wait for the next event.
  const size_t count = callbacks_.size();
  for (size_t i = 0; i < count && i < callbacks_.size(); i++) {
    if (callbacks_[i].removed || callbacks_[i].type != type ||
        callbacks_[i].phase != phase) {
      continue;
    }
    event.SetCurrentTarget(this);
    auto fn = callbacks_[i].fn;  // the callback may unregister itself
    fn(event);
    if (!*alive) return;  // the callback destroyed this element
    if (event.isImmediatePropagationStopped()) break;
  }
  if (--dispatch_depth_ == 0) {
    std::erase_if(callbacks_, [](const Callback& c) { return c.removed; });
  }
}

void VisualElement::SendEvent(EventBase& event) {
  if (panel_) {
    panel_->Dispatch(event, this);
    return;
  }
  auto alive = alive_;
  event.SetTarget(this);
  event.SetPhase(PropagationPhase::kAtTarget);
  InvokeCallbacks(event, TrickleDown::kTrickleDown);
  if (!*alive || event.isPropagationStopped()) return;
  HandleEventTrickleDown(event);
  if (!*alive) return;
  InvokeCallbacks(event, TrickleDown::kNoTrickleDown);
  if (!*alive || event.isPropagationStopped()) return;
  HandleEventBubbleUp(event);
}

Manipulator* VisualElement::AddManipulator(std::unique_ptr<Manipulator> manipulator) {
  Manipulator* raw = manipulator.get();
  manipulators_.push_back(std::move(manipulator));
  raw->SetTarget(this);
  return raw;
}

void VisualElement::RemoveManipulator(Manipulator* manipulator) {
  auto it = std::find_if(manipulators_.begin(), manipulators_.end(),
                         [manipulator](const auto& m) { return m.get() == manipulator; });
  if (it == manipulators_.end()) return;
  (*it)->SetTarget(nullptr);
  manipulators_.erase(it);
}

void VisualElement::CapturePointer(int pointer_id) {
  if (panel_) panel_->CapturePointer(this, pointer_id);
}

void VisualElement::ReleasePointer(int pointer_id) {
  if (panel_) panel_->ReleasePointer(this, pointer_id);
}

bool VisualElement::HasPointerCapture(int pointer_id) const {
  return panel_ && panel_->GetCapturingElement(pointer_id) == this;
}

// -------------------------------------------------------------- scheduling --

ScheduledItem& VisualElement::Schedule(std::function<void()> action) {
  auto item = std::make_unique<ScheduledItem>();
  item->action_ = std::move(action);
  scheduled_.push_back(std::move(item));
  return *scheduled_.back();
}

void VisualElement::RunScheduledItems(double now_ms) {
  if (scheduled_.empty()) return;
  auto alive = alive_;
  const size_t count = scheduled_.size();
  for (size_t i = 0; i < count && i < scheduled_.size(); i++) {
    ScheduledItem& item = *scheduled_[i];
    if (item.done_ || item.paused_) continue;
    if (item.started_at_ < 0.0) {
      item.started_at_ = now_ms;
      item.next_run_ = now_ms + item.delay_ms_;
    }
    if (now_ms < item.next_run_) continue;
    if (item.until_ && item.until_()) {
      item.done_ = true;
      continue;
    }
    auto action = item.action_;
    action();
    if (!*alive) return;
    ScheduledItem& after = *scheduled_[i];
    if (!after.repeat_ ||
        (after.duration_ms_ >= 0.0 && now_ms - after.started_at_ >= after.duration_ms_)) {
      after.done_ = true;
    } else {
      after.next_run_ = now_ms + after.interval_ms_;
    }
  }
  std::erase_if(scheduled_, [](const auto& item) { return item->done_; });
}

// ------------------------------------------------------------ data binding --

void VisualElement::SetDataSource(std::shared_ptr<IDataSource> source) {
  data_source_ = std::move(source);
  // Values may differ in the new source; make every binding below re-read.
  std::function<void(VisualElement*)> reset = [&](VisualElement* e) {
    for (auto& [property, binding] : e->bindings_) {
      binding->last_source.reset();
      binding->updated_once = false;
    }
    for (auto& child : e->children_) reset(child.get());
  };
  reset(this);
}

std::shared_ptr<IDataSource> VisualElement::ResolveDataSource() const {
  for (const VisualElement* e = this; e; e = e->hierarchy_parent_) {
    if (e->data_source_) return e->data_source_;
  }
  return nullptr;
}

void VisualElement::SetDataSourcePath(std::string path) {
  data_source_path_ = std::move(path);
}

std::string VisualElement::ResolveDataSourcePath() const {
  std::vector<const std::string*> parts;
  for (const VisualElement* e = this; e; e = e->hierarchy_parent_) {
    if (!e->data_source_path_.empty()) parts.push_back(&e->data_source_path_);
    // A data source on an element starts a new path.
    if (e->data_source_) break;
  }
  std::string out;
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    out += (out.empty() ? "" : ".") + **it;
  }
  return out;
}

void VisualElement::SetBinding(std::string_view property, DataBinding binding) {
  for (auto& [name, existing] : bindings_) {
    if (name == property) {
      *existing = std::move(binding);
      return;
    }
  }
  bindings_.emplace_back(std::string(property),
                         std::make_unique<DataBinding>(std::move(binding)));
}

void VisualElement::ClearBinding(std::string_view property) {
  std::erase_if(bindings_, [&](const auto& b) { return b.first == property; });
}

void VisualElement::ClearBindings() { bindings_.clear(); }

DataBinding* VisualElement::GetBinding(std::string_view property) {
  for (auto& [name, binding] : bindings_) {
    if (name == property) return binding.get();
  }
  return nullptr;
}

bool VisualElement::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "name") {
    SetName(VariantToString(value));
  } else if (name == "tooltip") {
    SetTooltip(VariantToString(value));
  } else if (name == "enabled") {
    if (auto b = VariantToBool(value)) SetEnabled(*b);
  } else if (name == "visible") {
    if (auto b = VariantToBool(value)) SetVisible(*b);
  } else if (name == "focusable") {
    if (auto b = VariantToBool(value)) SetFocusable(*b);
  } else if (name == "tabindex" || name == "tab-index") {
    if (auto i = VariantToInt(value)) SetTabIndex(static_cast<int>(*i));
  } else if (name == "picking-mode") {
    SetPickingMode(VariantToString(value) == "Ignore" ? PickingMode::kIgnore
                                                      : PickingMode::kPosition);
  } else if (name == "data-source-path") {
    SetDataSourcePath(VariantToString(value));
  } else {
    return false;
  }
  return true;
}

std::optional<mtext::Variant> VisualElement::GetProperty(std::string_view name) const {
  if (name == "name") return name_;
  if (name == "tooltip") return tooltip_;
  if (name == "enabled") return enabled_self_;
  if (name == "visible") return visible();
  if (name == "focusable") return focusable_;
  if (name == "tabindex" || name == "tab-index") return tab_index_;
  if (name == "picking-mode") {
    return std::string(picking_mode_ == PickingMode::kIgnore ? "Ignore" : "Position");
  }
  if (name == "data-source-path") return data_source_path_;
  return std::nullopt;
}

// ------------------------------------------------------------ manipulators --

void Manipulator::SetTarget(VisualElement* target) {
  if (target_) UnregisterCallbacksFromTarget();
  target_ = target;
  if (target_) RegisterCallbacksOnTarget();
}

Clickable::Clickable(std::function<void()> handler, double delay_ms,
                     double interval_ms)
    : clicked(std::move(handler)), delay_ms_(delay_ms), interval_ms_(interval_ms) {}

void Clickable::Invoke(EventBase& event) {
  if (clickedWithEventInfo) clickedWithEventInfo(event);
  if (clicked) clicked();
}

void Clickable::RegisterCallbacksOnTarget() {
  VisualElement* t = target();
  callbacks_.push_back(t->RegisterCallback<PointerDownEvent>([this](PointerDownEvent& e) {
    if (e.button != 0 || !target()->enabledInHierarchy()) return;
    active_ = true;
    target()->CapturePointer(e.pointerId);
    target()->SetPseudoState(kPseudoActive, true);
    e.StopPropagation();
    if (interval_ms_ > 0.0) {
      // Repeating (scroller arrows): fire now, then after the delay.
      Invoke(e);
      repeater_ = &target()->Schedule([this] {
                      if (!active_) return;
                      PointerDownEvent repeat;
                      Invoke(repeat);
                    })
                       .StartingIn(delay_ms_)
                       .Every(interval_ms_);
    }
  }));
  callbacks_.push_back(t->RegisterCallback<PointerMoveEvent>([this](PointerMoveEvent& e) {
    if (!active_) return;
    const bool over = target()->ContainsPoint(target()->WorldToLocal(e.position));
    target()->SetPseudoState(kPseudoActive, over);
    e.StopPropagation();
  }));
  callbacks_.push_back(t->RegisterCallback<PointerUpEvent>([this](PointerUpEvent& e) {
    if (!active_ || e.button != 0) return;
    active_ = false;
    target()->ReleasePointer(e.pointerId);
    target()->SetPseudoState(kPseudoActive, false);
    if (repeater_) {
      repeater_->Cancel();
      repeater_ = nullptr;
    }
    e.StopPropagation();
    if (interval_ms_ <= 0.0 &&
        target()->ContainsPoint(target()->WorldToLocal(e.position))) {
      Invoke(e);
    }
  }));
  callbacks_.push_back(
      t->RegisterCallback<PointerCaptureOutEvent>([this](PointerCaptureOutEvent&) {
        if (!active_) return;
        active_ = false;
        target()->SetPseudoState(kPseudoActive, false);
      }));
  // Keyboard and gamepad activation.
  callbacks_.push_back(
      t->RegisterCallback<NavigationSubmitEvent>([this](NavigationSubmitEvent& e) {
        if (!target()->enabledInHierarchy()) return;
        Invoke(e);
        e.StopPropagation();
      }));
}

void Clickable::UnregisterCallbacksFromTarget() {
  for (CallbackId id : callbacks_) target()->UnregisterCallback(id);
  callbacks_.clear();
  if (repeater_) {
    repeater_->Cancel();
    repeater_ = nullptr;
  }
}

}  // namespace Mercury::UI
