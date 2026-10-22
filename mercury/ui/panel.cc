#include "panel.h"

#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>

#include "ui/layout.h"
#include "ui/mesh_generator.h"
#include "ui/style_resolver.h"

namespace Mercury::UI {

namespace {

constexpr double kDoubleClickSeconds = 0.35;
constexpr float kDoubleClickDistance = 6.0f;

glm::mat3 Translation(glm::vec2 t) {
  glm::mat3 m(1.0f);
  m[2][0] = t.x;
  m[2][1] = t.y;
  return m;
}

glm::mat3 Scaling(float s) {
  glm::mat3 m(1.0f);
  m[0][0] = s;
  m[1][1] = s;
  return m;
}

using AliveList = std::vector<std::pair<VisualElement*, std::shared_ptr<const bool>>>;

// Snapshot of every element, so callbacks may change the tree safely.
AliveList CollectElements(VisualElement* root) {
  AliveList out;
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    out.emplace_back(e, e->aliveToken());
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(root);
  return out;
}

glm::ivec4 Intersect(glm::ivec4 a, glm::ivec4 b) {
  if (a.z < 0) return b;
  if (b.z < 0) return a;
  const int x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x + a.z, b.x + b.z), y1 = std::min(a.y + a.w, b.y + b.w);
  return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Paint(VisualElement* e, const glm::mat3& parent, float opacity,
           glm::ivec4 clip, MeshBuilder& builder) {
  const ComputedStyle& s = e->resolvedStyle();
  if (s.display == DisplayStyle::kNone) return;
  const float op = opacity * s.opacity;
  if (op <= 0.001f) return;
  const glm::mat3 m = parent * e->localTransform();
  builder.SetClip(clip);
  if (s.visibility == Visibility::kVisible) {
    MeshGenerationContext ctx(builder, e, m, op);
    DrawElementBox(ctx, *e);
    e->GenerateVisualContent(ctx);
    if (e->generateVisualContent) e->generateVisualContent(ctx);
  }
  if (e->hierarchyChildren().empty()) return;
  glm::ivec4 child_clip = clip;
  if (s.overflow == Overflow::kHidden) {
    const Rect r = e->paddingRect();
    glm::vec2 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
    for (glm::vec2 c : {glm::vec2(r.x, r.y), glm::vec2(r.xMax(), r.y),
                        glm::vec2(r.x, r.yMax()), glm::vec2(r.xMax(), r.yMax())}) {
      const glm::vec2 p = glm::vec2(m * glm::vec3(c, 1.0f));
      lo = glm::min(lo, p);
      hi = glm::max(hi, p);
    }
    const glm::ivec4 rect(static_cast<int>(std::floor(lo.x)), static_cast<int>(std::floor(lo.y)),
                          static_cast<int>(std::ceil(hi.x) - std::floor(lo.x)),
                          static_cast<int>(std::ceil(hi.y) - std::floor(lo.y)));
    child_clip = Intersect(clip, rect);
    if (child_clip.z <= 0 || child_clip.w <= 0) return;
  }
  const glm::mat3 child_m = m * Translation(e->childrenOffset());
  for (const auto& child : e->hierarchyChildren()) {
    Paint(child.get(), child_m, op, child_clip, builder);
  }
}

VisualElement* PickRec(VisualElement* e, glm::vec2 point, const glm::mat3& parent,
                       std::vector<VisualElement*>* all) {
  const ComputedStyle& s = e->resolvedStyle();
  if (s.display == DisplayStyle::kNone) return nullptr;
  const glm::mat3 m = parent * e->localTransform();
  const glm::vec2 local = glm::vec2(glm::inverse(m) * glm::vec3(point, 1.0f));
  const bool inside = e->ContainsPoint(local);
  if (s.overflow == Overflow::kHidden && !inside) return nullptr;
  const glm::mat3 child_m = m * Translation(e->childrenOffset());
  VisualElement* found = nullptr;
  const auto& children = e->hierarchyChildren();
  for (auto it = children.rbegin(); it != children.rend(); ++it) {
    if (VisualElement* hit = PickRec(it->get(), point, child_m, all)) {
      if (!all) return hit;
      if (!found) found = hit;
    }
  }
  if (!found && s.visibility == Visibility::kVisible &&
      e->pickingMode() == PickingMode::kPosition && inside) {
    found = e;
  }
  if (all && s.visibility == Visibility::kVisible &&
      e->pickingMode() == PickingMode::kPosition && inside) {
    all->push_back(e);
  }
  return found;
}

}  // namespace

float ComputePanelScale(const PanelSettings& settings, glm::vec2 target,
                        float dpi_scale) {
  float scale = 1.0f;
  switch (settings.scale_mode) {
    case PanelScaleMode::kConstantPixelSize: scale = 1.0f; break;
    case PanelScaleMode::kConstantPhysicalSize: {
      const float dpi = dpi_scale > 0 ? 96.0f * dpi_scale : settings.fallback_dpi;
      scale = dpi / std::max(1.0f, settings.reference_dpi);
      break;
    }
    case PanelScaleMode::kScaleWithScreenSize: {
      const glm::vec2 ref = glm::max(settings.reference_resolution, glm::vec2(1.0f));
      if (target.x <= 0 || target.y <= 0) break;
      switch (settings.screen_match_mode) {
        case PanelScreenMatchMode::kMatchWidthOrHeight: {
          const float lw = std::log2(target.x / ref.x);
          const float lh = std::log2(target.y / ref.y);
          scale = std::pow(2.0f, lw + (lh - lw) * std::clamp(settings.match, 0.0f, 1.0f));
          break;
        }
        case PanelScreenMatchMode::kShrink:
          scale = std::min(target.x / ref.x, target.y / ref.y);
          break;
        case PanelScreenMatchMode::kExpand:
          scale = std::max(target.x / ref.x, target.y / ref.y);
          break;
      }
      break;
    }
  }
  return std::max(0.01f, scale * settings.scale);
}

// --------------------------------------------------------- FocusController

void FocusController::SwitchFocus(VisualElement* element) {
  if (element == focused_) return;
  VisualElement* old = focused_;
  if (old) {
    std::shared_ptr<const bool> alive = old->aliveToken();
    old->SetPseudoState(kPseudoFocus, false);
    focused_ = nullptr;
    FocusOutEvent out;
    out.relatedTarget = element;
    panel_->Dispatch(out, old);
    if (*alive) {
      BlurEvent blur;
      blur.relatedTarget = element;
      panel_->Dispatch(blur, old);
    }
  }
  focused_ = element;
  if (element) {
    element->SetPseudoState(kPseudoFocus, true);
    FocusInEvent in;
    in.relatedTarget = old;
    panel_->Dispatch(in, element);
    if (focused_ == element) {
      FocusEvent focus;
      focus.relatedTarget = old;
      panel_->Dispatch(focus, element);
    }
  }
}

VisualElement* FocusController::FindNext(VisualElement* from, bool forward) const {
  std::vector<VisualElement*> order;
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    if (e->resolvedStyle().display == DisplayStyle::kNone) return;
    if (e->canGrabFocus() && e->tabIndex() >= 0) order.push_back(e);
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(panel_->visualTree());
  if (order.empty()) return nullptr;
  // Positive tab indices first (ascending), then 0 in tree order.
  std::stable_sort(order.begin(), order.end(), [](VisualElement* a, VisualElement* b) {
    const int ta = a->tabIndex() == 0 ? std::numeric_limits<int>::max() : a->tabIndex();
    const int tb = b->tabIndex() == 0 ? std::numeric_limits<int>::max() : b->tabIndex();
    return ta < tb;
  });
  auto it = std::find(order.begin(), order.end(), from);
  if (it == order.end()) return forward ? order.front() : order.back();
  const int n = static_cast<int>(order.size());
  const int i = static_cast<int>(it - order.begin());
  return order[(i + (forward ? 1 : n - 1)) % n];
}

VisualElement* FocusController::FindInDirection(VisualElement* from,
                                                NavigationDirection direction) const {
  if (direction == NavigationDirection::kNext) return FindNext(from, true);
  if (direction == NavigationDirection::kPrevious) return FindNext(from, false);
  if (!from) return FindNext(nullptr, true);
  glm::vec2 dir(0.0f);
  switch (direction) {
    case NavigationDirection::kLeft: dir = {-1, 0}; break;
    case NavigationDirection::kRight: dir = {1, 0}; break;
    case NavigationDirection::kUp: dir = {0, -1}; break;
    case NavigationDirection::kDown: dir = {0, 1}; break;
    default: return nullptr;
  }
  const glm::vec2 origin = from->worldBound().center();
  VisualElement* best = nullptr;
  float best_score = std::numeric_limits<float>::max();
  std::function<void(VisualElement*)> walk = [&](VisualElement* e) {
    if (e->resolvedStyle().display == DisplayStyle::kNone) return;
    if (e != from && e->canGrabFocus()) {
      const glm::vec2 d = e->worldBound().center() - origin;
      const float along = glm::dot(d, dir);
      if (along > 0.5f) {
        const float across = std::abs(dir.x != 0 ? d.y : d.x);
        const float score = along + across * 2.0f;
        if (score < best_score) {
          best_score = score;
          best = e;
        }
      }
    }
    for (const auto& child : e->hierarchyChildren()) walk(child.get());
  };
  walk(panel_->visualTree());
  return best;
}

void FocusController::OnDetached(VisualElement* element) {
  if (focused_ == element) focused_ = nullptr;
}

// ------------------------------------------------------------------ Panel

Panel::Panel(UIResources* resources) {
  if (!resources) {
    owned_resources_ = std::make_unique<UIResources>();
    resources = owned_resources_.get();
  }
  resources_ = resources;
  root_ = std::make_unique<VisualElement>();
  root_->SetName("rootVisualElement");
  root_->SetPickingMode(PickingMode::kIgnore);
  root_->SetPseudoState(kPseudoRoot, true);
  root_->SetPanel(this);
  clipboard.get = [this] { return fallback_clipboard_; };
  clipboard.set = [this](const std::string& text) { fallback_clipboard_ = text; };
}

Panel::~Panel() {
  // Elements notify the panel as they go; stop tracking first.
  hover_chain_.clear();
  hovered_ = nullptr;
  capture_ = nullptr;
  root_.reset();
}

void Panel::SetSettings(PanelSettings settings) {
  settings_ = std::move(settings);
  RestyleAll();
  layout_dirty_ = true;
}

void Panel::RestyleAll() {
  root_->MarkStyleDirty();
  style_dirty_ = true;
  repaint_ = true;
}

void Panel::OnElementDetached(VisualElement* element) {
  std::erase(hover_chain_, element);
  if (hovered_ == element) hovered_ = nullptr;
  if (capture_ == element) capture_ = nullptr;
  for (auto& target : press_target_) {
    if (target == element) target = nullptr;
  }
  if (focus_.focusedElement() == element) {
    element->SetPseudoState(kPseudoFocus, false);
    focus_.OnDetached(element);
    text_input_active_ = false;
  }
  repaint_ = true;
}

void Panel::CapturePointer(VisualElement* element, int pointer_id) {
  if (pointer_id != 0 || capture_ == element) return;
  if (capture_) {
    VisualElement* old = capture_;
    capture_ = nullptr;
    PointerCaptureOutEvent out;
    Dispatch(out, old);
  }
  capture_ = element;
  PointerCaptureEvent in;
  Dispatch(in, element);
}

void Panel::ReleasePointer(VisualElement* element, int pointer_id) {
  if (pointer_id != 0 || capture_ != element || !element) return;
  capture_ = nullptr;
  PointerCaptureOutEvent out;
  Dispatch(out, element);
}

VisualElement* Panel::GetCapturingElement(int pointer_id) const {
  return pointer_id == 0 ? capture_ : nullptr;
}

void Panel::Dispatch(EventBase& event, VisualElement* target, bool skip_disabled) {
  if (!target) return;
  event.SetTarget(target);
  event.SetTimestamp(time_);
  std::vector<VisualElement*> path;  // root first, target excluded
  for (VisualElement* p = target->hierarchyParent(); p; p = p->hierarchyParent()) {
    path.push_back(p);
  }
  std::reverse(path.begin(), path.end());
  std::vector<std::shared_ptr<const bool>> alive;
  for (VisualElement* e : path) alive.push_back(e->aliveToken());
  auto target_alive = target->aliveToken();
  auto usable = [&](VisualElement* e) {
    return !skip_disabled || e->enabledInHierarchy();
  };

  if (event.tricklesDown()) {
    event.SetPhase(PropagationPhase::kTrickleDown);
    for (size_t i = 0; i < path.size(); i++) {
      if (!*alive[i] || !usable(path[i])) continue;
      path[i]->InvokeCallbacks(event, TrickleDown::kTrickleDown);
      if (!*alive[i] || event.isPropagationStopped()) break;
      path[i]->HandleEventTrickleDown(event);
      if (event.isPropagationStopped()) break;
    }
  }
  if (!event.isPropagationStopped() && *target_alive && usable(target)) {
    event.SetPhase(PropagationPhase::kAtTarget);
    target->InvokeCallbacks(event, TrickleDown::kTrickleDown);
    if (*target_alive && !event.isImmediatePropagationStopped()) {
      target->HandleEventTrickleDown(event);
    }
    if (*target_alive && !event.isImmediatePropagationStopped()) {
      target->InvokeCallbacks(event, TrickleDown::kNoTrickleDown);
    }
    if (*target_alive && !event.isImmediatePropagationStopped()) {
      target->HandleEventBubbleUp(event);
    }
  }
  if (event.bubbles() && !event.isPropagationStopped()) {
    event.SetPhase(PropagationPhase::kBubbleUp);
    for (size_t i = path.size(); i-- > 0;) {
      if (!*alive[i] || !usable(path[i])) continue;
      path[i]->InvokeCallbacks(event, TrickleDown::kNoTrickleDown);
      if (!*alive[i] || event.isPropagationStopped()) break;
      path[i]->HandleEventBubbleUp(event);
      if (event.isPropagationStopped()) break;
    }
  }
  event.SetPhase(PropagationPhase::kNone);
  event.SetCurrentTarget(nullptr);
}

VisualElement* Panel::Pick(glm::vec2 point) const {
  return PickRec(root_.get(), point, glm::mat3(1.0f), nullptr);
}

std::vector<VisualElement*> Panel::PickAll(glm::vec2 point) const {
  std::vector<VisualElement*> all;
  PickRec(root_.get(), point, glm::mat3(1.0f), &all);
  // PickRec appends deepest-first per branch; order by depth, deepest first.
  return all;
}

template <typename E>
void Panel::FillPointer(E& event, glm::vec2 point, int button, KeyModifiers mods) {
  event.position = point;
  event.button = button;
  event.pressedButtons = pressed_buttons_;
  event.modifiers = mods;
  event.clickCount = click_count_;
  event.deltaPosition = pointer_known_ ? point - pointer_ : glm::vec2(0.0f);
}

void Panel::UpdateHover(glm::vec2 point, bool inside, KeyModifiers mods) {
  VisualElement* hit = inside ? Pick(point) : nullptr;
  std::vector<VisualElement*> chain;
  for (VisualElement* e = hit; e; e = e->hierarchyParent()) chain.push_back(e);
  const std::vector<VisualElement*> old_chain = hover_chain_;
  VisualElement* old_top = hovered_;
  hover_chain_ = chain;
  hovered_ = hit;

  for (VisualElement* e : old_chain) {
    if (std::find(chain.begin(), chain.end(), e) == chain.end()) {
      e->SetPseudoState(kPseudoHover, false);
    }
  }
  if (old_top != hit && old_top) {
    PointerOutEvent out;
    FillPointer(out, point, -1, mods);
    Dispatch(out, old_top, true);
  }
  for (VisualElement* e : old_chain) {
    if (std::find(chain.begin(), chain.end(), e) == chain.end() &&
        std::find(hover_chain_.begin(), hover_chain_.end(), e) == hover_chain_.end()) {
      PointerLeaveEvent leave;
      FillPointer(leave, point, -1, mods);
      // The element may have been removed by an earlier handler.
      if (e->panel() == this) Dispatch(leave, e, true);
    }
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    VisualElement* e = *it;
    if (std::find(old_chain.begin(), old_chain.end(), e) == old_chain.end()) {
      e->SetPseudoState(kPseudoHover, true);
      PointerEnterEvent enter;
      FillPointer(enter, point, -1, mods);
      if (e->panel() == this) Dispatch(enter, e, true);
    }
  }
  if (old_top != hit && hit && hit->panel() == this) {
    PointerOverEvent over;
    FillPointer(over, point, -1, mods);
    Dispatch(over, hit, true);
  }
  pointer_over_ui_ = hovered_ != nullptr;
}

void Panel::HandleNavigationKey(const UIInputEvent& input) {
  VisualElement* focused = focus_.focusedElement();
  VisualElement* target = focused ? focused : root_.get();
  switch (input.key) {
    case KeyCode::kTab: {
      const bool back = input.modifiers & kModShift;
      NavigationMoveEvent move;
      move.direction = back ? NavigationDirection::kPrevious : NavigationDirection::kNext;
      move.modifiers = input.modifiers;
      Dispatch(move, target, true);
      if (!move.isDefaultPrevented()) {
        if (VisualElement* next = focus_.FindNext(focused, !back)) next->Focus();
      }
      break;
    }
    case KeyCode::kLeft:
    case KeyCode::kRight:
    case KeyCode::kUp:
    case KeyCode::kDown: {
      NavigationMoveEvent move;
      move.modifiers = input.modifiers;
      move.direction = input.key == KeyCode::kLeft    ? NavigationDirection::kLeft
                       : input.key == KeyCode::kRight ? NavigationDirection::kRight
                       : input.key == KeyCode::kUp    ? NavigationDirection::kUp
                                                      : NavigationDirection::kDown;
      move.move = move.direction == NavigationDirection::kLeft    ? glm::vec2(-1, 0)
                  : move.direction == NavigationDirection::kRight ? glm::vec2(1, 0)
                  : move.direction == NavigationDirection::kUp    ? glm::vec2(0, -1)
                                                                  : glm::vec2(0, 1);
      Dispatch(move, target, true);
      if (!move.isPropagationStopped() && !move.isDefaultPrevented() && focused) {
        if (VisualElement* next = focus_.FindInDirection(focused, move.direction)) {
          next->Focus();
        }
      }
      break;
    }
    case KeyCode::kEnter:
    case KeyCode::kKeypadEnter: {
      NavigationSubmitEvent submit;
      Dispatch(submit, target, true);
      break;
    }
    case KeyCode::kEscape: {
      NavigationCancelEvent cancel;
      Dispatch(cancel, target, true);
      break;
    }
    default: break;
  }
}

void Panel::DispatchInput(const UIInputFrame& input) {
  const bool inside = input.pointer_inside && input.mouse_available;
  const glm::vec2 point = input.pointer / scale_;

  if (!pointer_known_ || point != pointer_ || !inside) {
    UpdateHover(point, inside, kModNone);
    VisualElement* target = capture_ ? capture_ : hovered_;
    if (inside && pointer_known_ && point != pointer_) {
      PointerMoveEvent move;
      FillPointer(move, point, -1, kModNone);
      Dispatch(move, target ? target : root_.get(), true);
    }
    pointer_ = point;
    pointer_known_ = inside;
  }

  for (const UIInputEvent& e : input.events) {
    switch (e.type) {
      case UIInputEvent::Type::kPointerDown: {
        if (!inside || e.button < 0 || e.button > 2) break;
        const bool repeat = time_ - last_click_time_ < kDoubleClickSeconds &&
                            glm::distance(point, last_click_pos_) < kDoubleClickDistance;
        click_count_ = repeat ? click_count_ + 1 : 1;
        last_click_time_ = time_;
        last_click_pos_ = point;
        pressed_buttons_ |= 1 << e.button;
        VisualElement* hit = Pick(point);
        VisualElement* target = capture_ ? capture_ : (hit ? hit : root_.get());
        press_target_[e.button] = target;
        if (e.button == 0) {
          // Focus the clicked element (or its nearest focusable ancestor).
          VisualElement* focusable = nullptr;
          for (VisualElement* f = hit; f; f = f->hierarchyParent()) {
            if (f->canGrabFocus() || (f->delegatesFocus() && f->focusable())) {
              focusable = f;
              break;
            }
          }
          if (focusable) {
            focusable->Focus();
          } else if (focus_.focusedElement()) {
            focus_.SwitchFocus(nullptr);
          }
        }
        PointerDownEvent down;
        FillPointer(down, point, e.button, e.modifiers);
        Dispatch(down, capture_ ? capture_ : target, true);
        break;
      }
      case UIInputEvent::Type::kPointerUp: {
        if (e.button < 0 || e.button > 2) break;
        pressed_buttons_ &= ~(1 << e.button);
        VisualElement* hit = inside ? Pick(point) : nullptr;
        VisualElement* target = capture_ ? capture_ : (hit ? hit : root_.get());
        PointerUpEvent up;
        FillPointer(up, point, e.button, e.modifiers);
        Dispatch(up, target, true);
        VisualElement* pressed = press_target_[e.button];
        press_target_[e.button] = nullptr;
        if (pressed && hit && pressed->Contains(hit) && pressed->panel() == this) {
          ClickEvent click;
          FillPointer(click, point, e.button, e.modifiers);
          Dispatch(click, pressed, true);
        }
        break;
      }
      case UIInputEvent::Type::kWheel: {
        if (!inside) break;
        VisualElement* hit = Pick(point);
        WheelEvent wheel;
        FillPointer(wheel, point, -1, e.modifiers);
        wheel.delta = e.wheel;
        Dispatch(wheel, hit ? hit : root_.get(), true);
        break;
      }
      case UIInputEvent::Type::kKeyDown: {
        if (!input.keyboard_available) break;
        VisualElement* target = focus_.focusedElement();
        KeyDownEvent key;
        key.keyCode = e.key;
        key.modifiers = e.modifiers;
        Dispatch(key, target ? target : root_.get(), true);
        if (!key.isDefaultPrevented() && !key.isPropagationStopped()) {
          HandleNavigationKey(e);
        }
        break;
      }
      case UIInputEvent::Type::kKeyUp: {
        if (!input.keyboard_available) break;
        VisualElement* target = focus_.focusedElement();
        KeyUpEvent key;
        key.keyCode = e.key;
        key.modifiers = e.modifiers;
        Dispatch(key, target ? target : root_.get(), true);
        break;
      }
      case UIInputEvent::Type::kChar: {
        if (!input.keyboard_available || e.character < 32 || e.character == 127) break;
        VisualElement* target = focus_.focusedElement();
        if (!target) break;
        KeyDownEvent key;
        key.character = e.character;
        key.modifiers = e.modifiers;
        Dispatch(key, target, true);
        break;
      }
    }
  }
}

void Panel::UpdateScale(glm::vec2 target_size, float dpi_scale) {
  const float scale = ComputePanelScale(settings_, target_size, dpi_scale);
  const glm::vec2 size = target_size / scale;
  if (scale != scale_ || size != size_ || target_size != target_size_) {
    scale_ = scale;
    size_ = size;
    target_size_ = target_size;
    layout_dirty_ = true;
    repaint_ = true;
  }
}

void Panel::Update(double dt_seconds, glm::vec2 target_size, float dpi_scale) {
  time_ += dt_seconds;
  UpdateScale(target_size, dpi_scale);
  if (resources_->version() != resources_version_) {
    resources_version_ = resources_->version();
    RestyleAll();
  }
  if (Font::AtlasResizeCount() != atlas_resizes_) {
    atlas_resizes_ = Font::AtlasResizeCount();
    repaint_ = true;
  }

  // Scheduled actions and bindings may change the tree, so walk a snapshot.
  const double now_ms = time_ * 1000.0;
  for (auto& [element, alive] : CollectElements(root_.get())) {
    if (*alive) element->RunScheduledItems(now_ms);
    if (*alive && !element->bindings().empty()) element->UpdateBindings();
  }

  // Twice at most: geometry callbacks often adjust styles.
  for (int pass = 0; pass < 2; pass++) {
    if (style_dirty_) {
      style_dirty_ = false;
      std::vector<const StyleSheet*> sheets;
      std::shared_ptr<StyleSheet> theme =
          settings_.theme_style_sheet.empty()
              ? resources_->DefaultTheme()
              : resources_->LoadStyleSheet(settings_.theme_style_sheet);
      if (theme) resources_->CollectSheets(theme, sheets);
      ResolveStyles(root_.get(), *resources_, sheets, true);
    }
    bool animating = false;
    for (auto& [element, alive] : CollectElements(root_.get())) {
      if (*alive && element->HasRunningTransitions()) {
        animating |= element->UpdateTransitions(time_);
      }
    }
    if (animating) repaint_ = true;

    if (layout_dirty_ || YGNodeIsDirtyRoot()) {
      layout_dirty_ = false;
      std::vector<GeometryChange> changes;
      CalculateLayout(root_.get(), size_.x, size_.y, changes);
      if (!changes.empty()) repaint_ = true;
      for (const auto& change : changes) {
        GeometryChangedEvent event;
        event.oldRect = change.old_rect;
        event.newRect = change.new_rect;
        if (change.element->panel() == this) Dispatch(event, change.element);
      }
    }
    if (!style_dirty_ && !layout_dirty_) break;
  }

  if (repaint_) {
    repaint_ = false;
    Repaint();
  }
}

bool Panel::YGNodeIsDirtyRoot() const { return IsLayoutDirty(root_.get()); }

void Panel::Repaint() {
  draw_data_.Clear();
  draw_data_.target_size = glm::ivec2(target_size_);
  MeshBuilder builder(draw_data_);
  const glm::ivec4 full(0, 0, static_cast<int>(target_size_.x),
                        static_cast<int>(target_size_.y));
  Paint(root_.get(), Scaling(scale_), 1.0f, full, builder);
}

}  // namespace Mercury::UI
