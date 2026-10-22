#include "controls.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "ui/mesh_generator.h"
#include "ui/panel.h"
#include "ui/uxml.h"

namespace Mercury::UI {

namespace {

bool IsWordChar(char32_t c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         c == '_' || c > 127;
}

template <typename E>
bool Is(const EventBase& e) {
  return e.eventTypeId() == E::TypeId();
}

std::string FormatFloat(float v) {
  std::string s = std::format("{:.4f}", v);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  return s.empty() ? "0" : s;
}

}  // namespace

// ------------------------------------------------------------ TextElement --

TextElement::TextElement() { AddToClassList(ussClassName); }

void TextElement::SetText(std::string text) {
  if (text == text_) return;
  text_ = std::move(text);
  MarkMeasureDirty();
  NotifyPropertyChanged();
}

void TextElement::SetEnableRichText(bool enable) {
  if (enable == rich_text_) return;
  rich_text_ = enable;
  MarkMeasureDirty();
}

Font* TextElement::ResolveFont() const {
  if (!panel()) return nullptr;
  return panel()->resources().LoadFont(resolvedStyle().unityFontDefinition.path).get();
}

TextSettings TextElement::MakeTextSettings() const {
  const ComputedStyle& s = resolvedStyle();
  TextSettings settings;
  settings.font = ResolveFont();
  settings.size = s.fontSize;
  settings.color = s.color;
  settings.font_style = s.unityFontStyleAndWeight;
  settings.align = s.unityTextAlign;
  settings.white_space = s.whiteSpace;
  settings.overflow = s.textOverflow;
  settings.letter_spacing = s.letterSpacing;
  settings.word_spacing = s.wordSpacing;
  settings.paragraph_spacing = s.unityParagraphSpacing;
  settings.rich_text = rich_text_;
  return settings;
}

glm::vec2 TextElement::Measure(float width, MeasureMode width_mode, float height,
                               MeasureMode height_mode) {
  TextSettings settings = MakeTextSettings();
  if (!settings.font) return glm::vec2(0.0f);
  const bool wraps = settings.white_space == WhiteSpace::kNormal ||
                     settings.white_space == WhiteSpace::kPreWrap;
  float max_width = std::numeric_limits<float>::infinity();
  if (wraps && width_mode != MeasureMode::kUndefined) max_width = width;
  const glm::vec2 measured = MeasureText(DisplayText(), settings, max_width);
  glm::vec2 out(std::ceil(measured.x), std::ceil(measured.y));
  if (width_mode == MeasureMode::kExactly) out.x = width;
  if (width_mode == MeasureMode::kAtMost) out.x = std::min(out.x, width);
  if (height_mode == MeasureMode::kExactly) out.y = height;
  if (height_mode == MeasureMode::kAtMost) out.y = std::min(out.y, height);
  return out;
}

glm::vec2 TextElement::TextOrigin(const TextLayout& layout) const {
  const Rect content = contentRect();
  const int row = static_cast<int>(resolvedStyle().unityTextAlign) / 3;
  const float free = content.height - layout.size.y;
  const float y = row == 0 ? 0.0f : row == 1 ? free / 2.0f : free;
  return {content.x, content.y + y};
}

void TextElement::GenerateVisualContent(MeshGenerationContext& context) {
  const std::string text = DisplayText();
  if (text.empty()) return;
  TextSettings settings = MakeTextSettings();
  if (!settings.font) return;
  const Rect content = contentRect();
  TextLayout layout = LayoutText(text, settings, content.width);
  context.DrawText(layout, *settings.font, TextOrigin(layout), resolvedStyle());
}

bool TextElement::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "text") {
    SetText(VariantToString(value));
    return true;
  }
  if (name == "enable-rich-text") {
    if (auto b = VariantToBool(value)) SetEnableRichText(*b);
    return true;
  }
  return VisualElement::SetProperty(name, value);
}

std::optional<mtext::Variant> TextElement::GetProperty(std::string_view name) const {
  if (name == "text") return text_;
  if (name == "enable-rich-text") return rich_text_;
  return VisualElement::GetProperty(name);
}

Label::Label() { AddToClassList(ussClassName); }

Button::Button() {
  AddToClassList(ussClassName);
  SetFocusable(true);
  clickable_ = static_cast<Clickable*>(
      AddManipulator(std::make_unique<Clickable>([this] { clicked(); })));
}

RepeatButton::RepeatButton() {
  AddToClassList(ussClassName);
  SetFocusable(true);
}

RepeatButton::RepeatButton(std::function<void()> action, double delay_ms,
                           double interval_ms)
    : RepeatButton() {
  SetAction(std::move(action), delay_ms, interval_ms);
}

void RepeatButton::SetAction(std::function<void()> action, double delay_ms,
                             double interval_ms) {
  if (clickable_) RemoveManipulator(clickable_);
  clickable_ = static_cast<Clickable*>(AddManipulator(
      std::make_unique<Clickable>(std::move(action), delay_ms, std::max(1.0, interval_ms))));
}

// ------------------------------------------------------------------ Image --

Image::Image() { AddToClassList(ussClassName); }

void Image::SetImage(std::string path) {
  path_ = std::move(path);
  texture_id_ = 0;
  MarkMeasureDirty();
  NotifyPropertyChanged();
}

void Image::SetTexture(uint32_t texture_id) {
  texture_id_ = texture_id;
  path_.clear();
  MarkMeasureDirty();
}

void Image::SetScaleMode(ScaleMode mode) {
  scale_mode_ = mode;
  MarkDirtyRepaint();
}

void Image::SetTintColor(Color tint) {
  tint_ = tint;
  MarkDirtyRepaint();
}

void Image::SetUV(glm::vec4 uv) {
  uv_ = uv;
  MarkMeasureDirty();
}

std::shared_ptr<UITexture> Image::Texture() const {
  if (texture_id_) return UITextureRegistry::Instance().Find(texture_id_);
  if (path_.empty() || !panel()) return nullptr;
  return panel()->resources().LoadTexture(path_);
}

glm::vec2 Image::Measure(float width, MeasureMode width_mode, float height,
                         MeasureMode height_mode) {
  auto texture = Texture();
  if (!texture) return glm::vec2(0.0f);
  glm::vec2 size(texture->width * uv_.z, texture->height * uv_.w);
  const float aspect = size.y > 0 ? size.x / size.y : 1.0f;
  if (width_mode == MeasureMode::kExactly && height_mode != MeasureMode::kExactly) {
    size = {width, width / aspect};
  } else if (height_mode == MeasureMode::kExactly && width_mode != MeasureMode::kExactly) {
    size = {height * aspect, height};
  }
  if (width_mode == MeasureMode::kExactly) size.x = width;
  if (height_mode == MeasureMode::kExactly) size.y = height;
  if (width_mode == MeasureMode::kAtMost) size.x = std::min(size.x, width);
  if (height_mode == MeasureMode::kAtMost) size.y = std::min(size.y, height);
  return size;
}

void Image::GenerateVisualContent(MeshGenerationContext& context) {
  auto texture = Texture();
  if (!texture) return;
  const Rect box = contentRect();
  Rect rect = box;
  glm::vec4 uv(uv_.x, uv_.y, uv_.x + uv_.z, uv_.y + uv_.w);
  const float tw = texture->width * uv_.z, th = texture->height * uv_.w;
  if (tw <= 0 || th <= 0 || box.width <= 0 || box.height <= 0) return;
  const float box_aspect = box.width / box.height, tex_aspect = tw / th;
  if (scale_mode_ == ScaleMode::kScaleToFit) {
    if (tex_aspect > box_aspect) {
      rect.height = box.width / tex_aspect;
      rect.y += (box.height - rect.height) / 2;
    } else {
      rect.width = box.height * tex_aspect;
      rect.x += (box.width - rect.width) / 2;
    }
  } else if (scale_mode_ == ScaleMode::kScaleAndCrop) {
    if (tex_aspect > box_aspect) {
      const float visible = box_aspect / tex_aspect * uv_.z;
      uv.x = uv_.x + (uv_.z - visible) / 2;
      uv.z = uv.x + visible;
    } else {
      const float visible = tex_aspect / box_aspect * uv_.w;
      uv.y = uv_.y + (uv_.w - visible) / 2;
      uv.w = uv.y + visible;
    }
  }
  context.DrawImage(rect, texture->id, uv, tint_);
}

bool Image::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "image" || name == "source") {
    if (auto r = value.Get<mtext::Resource>()) {
      SetImage(r->path);
    } else {
      SetImage(VariantToString(value));
    }
    return true;
  }
  if (name == "scale-mode") {
    const std::string mode = VariantToString(value);
    SetScaleMode(mode == "StretchToFill"  ? ScaleMode::kStretchToFill
                 : mode == "ScaleAndCrop" ? ScaleMode::kScaleAndCrop
                                          : ScaleMode::kScaleToFit);
    return true;
  }
  if (name == "tint-color") {
    if (auto c = VariantToColor(value)) SetTintColor(*c);
    return true;
  }
  return VisualElement::SetProperty(name, value);
}

std::optional<mtext::Variant> Image::GetProperty(std::string_view name) const {
  if (name == "image" || name == "source") return path_;
  if (name == "scale-mode") {
    return std::string(scale_mode_ == ScaleMode::kStretchToFill  ? "StretchToFill"
                       : scale_mode_ == ScaleMode::kScaleAndCrop ? "ScaleAndCrop"
                                                                 : "ScaleToFit");
  }
  if (name == "tint-color") return mtext::Color{tint_.rgba};
  return VisualElement::GetProperty(name);
}

// --------------------------------------------------------------- GroupBox --

GroupBox::GroupBox() { AddToClassList(ussClassName); }

const std::string& GroupBox::text() const {
  static const std::string kEmpty;
  return label_ ? label_->text() : kEmpty;
}

void GroupBox::SetText(std::string text) {
  if (!label_) {
    auto label = std::make_unique<Label>();
    label->AddToClassList("unity-group-box__label");
    label_ = static_cast<Label*>(hierarchy().Insert(0, std::move(label)));
  }
  label_->SetText(std::move(text));
}

bool GroupBox::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "text") {
    SetText(VariantToString(value));
    return true;
  }
  return VisualElement::SetProperty(name, value);
}

std::optional<mtext::Variant> GroupBox::GetProperty(std::string_view name) const {
  if (name == "text") return text();
  return VisualElement::GetProperty(name);
}

// -------------------------------------------------------------- BaseField --

template <typename T>
BaseField<T>::BaseField() {
  this->AddToClassList(ussClassName);
  auto input = std::make_unique<VisualElement>();
  input->AddToClassList(inputUssClassName);
  input->SetPickingMode(PickingMode::kPosition);
  input_ = this->hierarchy().Add(std::move(input));
}

template <typename T>
void BaseField<T>::SetLabel(std::string text) {
  label_text_ = std::move(text);
  if (label_text_.empty()) {
    if (label_) {
      label_->RemoveFromHierarchy();
      label_ = nullptr;
    }
    this->RemoveFromClassList("unity-base-field--with-label");
    return;
  }
  if (!label_) {
    auto label = std::make_unique<Label>();
    label->AddToClassList(labelUssClassName);
    label_ = static_cast<Label*>(this->hierarchy().Insert(0, std::move(label)));
  }
  label_->SetText(label_text_);
  this->AddToClassList("unity-base-field--with-label");
}

template class BaseField<bool>;
template class BaseField<float>;
template class BaseField<int>;
template class BaseField<std::string>;

// ----------------------------------------------------------------- Toggle --

Toggle::Toggle() {
  AddToClassList(ussClassName);
  SetFocusable(true);
  input_->AddToClassList("unity-toggle__input");
  auto checkmark = std::make_unique<VisualElement>();
  checkmark->AddToClassList("unity-toggle__checkmark");
  checkmark_ = input_->Add(std::move(checkmark));
}

void Toggle::SetText(std::string text) {
  text_ = std::move(text);
  if (!text_label_) {
    auto label = std::make_unique<Label>();
    label->AddToClassList("unity-toggle__text");
    text_label_ = static_cast<Label*>(input_->Add(std::move(label)));
  }
  text_label_->SetText(text_);
}

void Toggle::OnValueChanged() {
  SetPseudoState(kPseudoChecked, value_);
  input_->SetPseudoState(kPseudoChecked, value_);
  checkmark_->SetPseudoState(kPseudoChecked, value_);
}

void Toggle::HandleEventBubbleUp(EventBase& event) {
  if ((Is<ClickEvent>(event) || Is<NavigationSubmitEvent>(event)) &&
      enabledInHierarchy()) {
    SetValue(!value_);
    event.StopPropagation();
  }
}

bool Toggle::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value") {
    if (auto b = VariantToBool(value)) SetValue(*b);
    return true;
  }
  if (name == "label") {
    SetLabel(VariantToString(value));
    return true;
  }
  if (name == "text") {
    SetText(VariantToString(value));
    return true;
  }
  return VisualElement::SetProperty(name, value);
}

std::optional<mtext::Variant> Toggle::GetProperty(std::string_view name) const {
  if (name == "value") return value_;
  if (name == "label") return label();
  if (name == "text") return text_;
  return VisualElement::GetProperty(name);
}

RadioButton::RadioButton() {
  RemoveFromClassList(Toggle::ussClassName);
  AddToClassList(ussClassName);
  input_->RemoveFromClassList("unity-toggle__input");
  input_->AddToClassList("unity-radio-button__input");
  checkmark_->RemoveFromClassList("unity-toggle__checkmark");
  checkmark_->AddToClassList("unity-radio-button__checkmark-background");
  auto dot = std::make_unique<VisualElement>();
  dot->AddToClassList("unity-radio-button__checkmark");
  checkmark_->Add(std::move(dot));
}

void RadioButton::HandleEventBubbleUp(EventBase& event) {
  if ((Is<ClickEvent>(event) || Is<NavigationSubmitEvent>(event)) &&
      enabledInHierarchy()) {
    SetValue(true);
    event.StopPropagation();
  }
}

void RadioButton::OnValueChanged() {
  Toggle::OnValueChanged();
  if (!value_ || !parent()) return;
  // Radio buttons that share a parent form a group.
  for (VisualElement* sibling : parent()->Children()) {
    if (sibling == this) continue;
    if (auto* radio = dynamic_cast<RadioButton*>(sibling)) radio->SetValue(false);
  }
}

// ------------------------------------------------------------- BaseSlider --

template <typename T>
BaseSlider<T>::BaseSlider(T low, T high) : low_(low), high_(high) {
  this->AddToClassList(ussClassName);
  this->AddToClassList("unity-base-slider--horizontal");
  this->SetFocusable(true);
  this->input_->AddToClassList("unity-base-slider__input");
  auto container = std::make_unique<VisualElement>();
  container->AddToClassList("unity-base-slider__drag-container");
  drag_container_ = this->input_->Add(std::move(container));
  auto tracker = std::make_unique<VisualElement>();
  tracker->AddToClassList("unity-base-slider__tracker");
  tracker_ = drag_container_->Add(std::move(tracker));
  auto dragger = std::make_unique<VisualElement>();
  dragger->AddToClassList("unity-base-slider__dragger");
  dragger_ = drag_container_->Add(std::move(dragger));
  this->value_ = low;
  OnValueChanged();
}

template <typename T>
void BaseSlider<T>::SetLowValue(T low) {
  low_ = low;
  this->SetValueWithoutNotify(this->value_);
}

template <typename T>
void BaseSlider<T>::SetHighValue(T high) {
  high_ = high;
  this->SetValueWithoutNotify(this->value_);
}

template <typename T>
void BaseSlider<T>::SetDirection(SliderDirection direction) {
  direction_ = direction;
  const bool h = direction == SliderDirection::kHorizontal;
  this->EnableInClassList("unity-base-slider--horizontal", h);
  this->EnableInClassList("unity-base-slider--vertical", !h);
  OnValueChanged();
}

template <typename T>
void BaseSlider<T>::SetInverted(bool inverted) {
  inverted_ = inverted;
  OnValueChanged();
}

template <typename T>
float BaseSlider<T>::normalizedValue() const {
  const float range = static_cast<float>(high_ - low_);
  if (range == 0.0f) return 0.0f;
  return std::clamp(static_cast<float>(this->value_ - low_) / range, 0.0f, 1.0f);
}

template <typename T>
T BaseSlider<T>::Coerce(T value) const {
  const T lo = std::min(low_, high_), hi = std::max(low_, high_);
  return std::clamp(value, lo, hi);
}

template <typename T>
void BaseSlider<T>::OnValueChanged() {
  if (!dragger_) return;
  float t = normalizedValue();
  if (inverted_) t = 1.0f - t;
  auto& s = dragger_->style();
  if (direction_ == SliderDirection::kHorizontal) {
    s.left = Length::Percent(t * 100.0f);
    s.top.reset();
  } else {
    s.top = Length::Percent(t * 100.0f);
    s.left.reset();
  }
}

template <typename T>
void BaseSlider<T>::SetFromPointer(glm::vec2 world) {
  const glm::vec2 local = drag_container_->WorldToLocal(world);
  const Rect& r = drag_container_->layout();
  float t = direction_ == SliderDirection::kHorizontal
                ? (r.width > 0 ? local.x / r.width : 0.0f)
                : (r.height > 0 ? local.y / r.height : 0.0f);
  t = std::clamp(t, 0.0f, 1.0f);
  if (inverted_) t = 1.0f - t;
  this->SetValue(FromNormalized(t));
}

template <typename T>
void BaseSlider<T>::HandleEventBubbleUp(EventBase& event) {
  if (!this->enabledInHierarchy()) return;
  if (Is<PointerDownEvent>(event)) {
    auto& e = static_cast<PointerDownEvent&>(event);
    if (e.button != 0 || !this->input_->Contains(e.target())) return;
    dragging_ = true;
    this->CapturePointer(e.pointerId);
    SetFromPointer(e.position);
    event.StopPropagation();
  } else if (Is<PointerMoveEvent>(event) && dragging_) {
    SetFromPointer(static_cast<PointerMoveEvent&>(event).position);
    event.StopPropagation();
  } else if (Is<PointerUpEvent>(event) && dragging_) {
    dragging_ = false;
    this->ReleasePointer(static_cast<PointerUpEvent&>(event).pointerId);
    event.StopPropagation();
  } else if (Is<PointerCaptureOutEvent>(event)) {
    dragging_ = false;
  } else if (Is<NavigationMoveEvent>(event)) {
    auto& e = static_cast<NavigationMoveEvent&>(event);
    const bool h = direction_ == SliderDirection::kHorizontal;
    float dir = 0.0f;
    if (h && e.direction == NavigationDirection::kLeft) dir = -1;
    if (h && e.direction == NavigationDirection::kRight) dir = 1;
    if (!h && e.direction == NavigationDirection::kUp) dir = -1;
    if (!h && e.direction == NavigationDirection::kDown) dir = 1;
    if (dir == 0.0f) return;
    if (inverted_) dir = -dir;
    float step = page_size_ > 0 ? page_size_ : static_cast<float>(high_ - low_) / 100.0f;
    if constexpr (std::is_integral_v<T>) step = std::max(1.0f, std::round(step));
    this->SetValue(static_cast<T>(this->value_ + dir * step));
    event.StopPropagation();
  }
}

template <typename T>
bool BaseSlider<T>::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value") {
    if (auto f = VariantToFloat(value)) this->SetValue(static_cast<T>(*f));
    return true;
  }
  if (name == "low-value") {
    if (auto f = VariantToFloat(value)) SetLowValue(static_cast<T>(*f));
    return true;
  }
  if (name == "high-value") {
    if (auto f = VariantToFloat(value)) SetHighValue(static_cast<T>(*f));
    return true;
  }
  if (name == "page-size") {
    if (auto f = VariantToFloat(value)) SetPageSize(static_cast<float>(*f));
    return true;
  }
  if (name == "direction") {
    SetDirection(VariantToString(value) == "Vertical" ? SliderDirection::kVertical
                                                      : SliderDirection::kHorizontal);
    return true;
  }
  if (name == "inverted") {
    if (auto b = VariantToBool(value)) SetInverted(*b);
    return true;
  }
  if (name == "label") {
    this->SetLabel(VariantToString(value));
    return true;
  }
  return VisualElement::SetProperty(name, value);
}

template <typename T>
std::optional<mtext::Variant> BaseSlider<T>::GetProperty(std::string_view name) const {
  if (name == "value") return mtext::Variant(this->value_);
  if (name == "low-value") return mtext::Variant(low_);
  if (name == "high-value") return mtext::Variant(high_);
  if (name == "page-size") return mtext::Variant(page_size_);
  if (name == "direction") {
    return std::string(direction_ == SliderDirection::kVertical ? "Vertical" : "Horizontal");
  }
  if (name == "inverted") return inverted_;
  if (name == "label") return this->label();
  return VisualElement::GetProperty(name);
}

template class BaseSlider<float>;
template class BaseSlider<int>;

Slider::Slider(float low, float high) : BaseSlider(low, high) {
  AddToClassList(ussClassName);
}

float Slider::FromNormalized(float t) const { return low_ + t * (high_ - low_); }

SliderInt::SliderInt(int low, int high) : BaseSlider(low, high) {
  AddToClassList(ussClassName);
  page_size_ = 1.0f;
}

int SliderInt::FromNormalized(float t) const {
  return static_cast<int>(std::round(low_ + t * (high_ - low_)));
}

// ------------------------------------------------------------ ProgressBar --

ProgressBar::ProgressBar() {
  AddToClassList(ussClassName);
  auto container = std::make_unique<VisualElement>();
  container->AddToClassList("unity-progress-bar__container");
  VisualElement* c = hierarchy().Add(std::move(container));
  auto background = std::make_unique<VisualElement>();
  background->AddToClassList("unity-progress-bar__background");
  VisualElement* bg = c->Add(std::move(background));
  auto progress = std::make_unique<VisualElement>();
  progress->AddToClassList("unity-progress-bar__progress");
  progress_ = bg->Add(std::move(progress));
  auto title_container = std::make_unique<VisualElement>();
  title_container->AddToClassList("unity-progress-bar__title-container");
  VisualElement* tc = bg->Add(std::move(title_container));
  auto title = std::make_unique<Label>();
  title->AddToClassList("unity-progress-bar__title");
  title_ = static_cast<Label*>(tc->Add(std::move(title)));
  UpdateProgress();
}

void ProgressBar::SetLowValue(float v) {
  low_ = v;
  UpdateProgress();
}
void ProgressBar::SetHighValue(float v) {
  high_ = v;
  UpdateProgress();
}
void ProgressBar::SetValue(float v) {
  if (v == value_) return;
  value_ = v;
  UpdateProgress();
  NotifyPropertyChanged();
}
const std::string& ProgressBar::title() const { return title_->text(); }
void ProgressBar::SetTitle(std::string title) { title_->SetText(std::move(title)); }

void ProgressBar::UpdateProgress() {
  const float range = high_ - low_;
  const float t = range != 0 ? std::clamp((value_ - low_) / range, 0.0f, 1.0f) : 0.0f;
  progress_->style().width = Length::Percent(t * 100.0f);
}

bool ProgressBar::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value") {
    if (auto f = VariantToFloat(value)) SetValue(static_cast<float>(*f));
  } else if (name == "low-value") {
    if (auto f = VariantToFloat(value)) SetLowValue(static_cast<float>(*f));
  } else if (name == "high-value") {
    if (auto f = VariantToFloat(value)) SetHighValue(static_cast<float>(*f));
  } else if (name == "title") {
    SetTitle(VariantToString(value));
  } else {
    return VisualElement::SetProperty(name, value);
  }
  return true;
}

std::optional<mtext::Variant> ProgressBar::GetProperty(std::string_view name) const {
  if (name == "value") return mtext::Variant(value_);
  if (name == "low-value") return mtext::Variant(low_);
  if (name == "high-value") return mtext::Variant(high_);
  if (name == "title") return title();
  return VisualElement::GetProperty(name);
}

// -------------------------------------------------------------- TextField --

/**
 * @brief The element inside a TextField that shows the text, caret and
 * selection
 */
class TextInputBase : public TextElement {
 public:
  explicit TextInputBase(TextField* field) : field_(field) {
    AddToClassList("unity-text-element--inner-input-field-component");
    SetEnableRichText(false);
    SetPickingMode(PickingMode::kIgnore);
  }
  const char* typeName() const override { return "TextElement"; }
  std::string DisplayText() const override { return field_->DisplayText(); }
  void GenerateVisualContent(MeshGenerationContext& context) override {
    field_->DrawCaretAndSelection(context);
  }
  glm::vec2 Measure(float width, MeasureMode width_mode, float height,
                    MeasureMode height_mode) override {
    glm::vec2 size = TextElement::Measure(width, width_mode, height, height_mode);
    TextSettings settings = MakeTextSettings();
    if (settings.font && height_mode != MeasureMode::kExactly) {
      // Never collapse below one line, even when empty.
      size.y = std::max(size.y, std::ceil(settings.font->lineHeight() *
                                          settings.size / kSdfBaseSize));
    }
    return size;
  }
  // Re-measure and redraw after the field's text or mode changed.
  void Refresh() { MarkMeasureDirty(); }
  float scroll_x = 0.0f;

 private:
  TextField* field_;
};

TextField::TextField() {
  AddToClassList("unity-base-text-field");
  AddToClassList(ussClassName);
  SetFocusable(true);
  input_->AddToClassList("unity-base-text-field__input");
  input_->AddToClassList("unity-text-field__input");
  text_input_ = static_cast<TextInputBase*>(
      input_->Add(std::make_unique<TextInputBase>(this)));
}

void TextField::SetMultiline(bool multiline) {
  multiline_ = multiline;
  EnableInClassList("unity-text-field--multiline", multiline);
  text_input_->MarkStyleDirty();
}

void TextField::SetIsPasswordField(bool password) {
  password_ = password;
  text_input_->Refresh();
}

void TextField::SetPlaceholder(std::string text) {
  placeholder_ = std::move(text);
  text_input_->Refresh();
}

std::string TextField::Coerce(std::string value) const {
  if (!multiline_) std::erase(value, '\n');
  if (max_length_ >= 0) {
    std::u32string text = DecodeUtf8(value);
    if (static_cast<int>(text.size()) > max_length_) {
      text.resize(max_length_);
      value = EncodeUtf8(text);
    }
  }
  return value;
}

void TextField::SetValueWithoutNotify(std::string value) {
  BaseField::SetValueWithoutNotify(std::move(value));
  editing_ = DecodeUtf8(value_);
  const int n = static_cast<int>(editing_.size());
  cursor_ = std::clamp(cursor_, 0, n);
  anchor_ = std::clamp(anchor_, 0, n);
  text_input_->Refresh();
}

std::string TextField::DisplayText() const {
  if (editing_.empty() && !IsFocused() && !placeholder_.empty()) return placeholder_;
  if (password_) return EncodeUtf8(std::u32string(editing_.size(), mask_char_));
  return EncodeUtf8(editing_);
}

void TextField::CommitText(const std::string& text) { SetValue(text); }

void TextField::ChangeText(std::u32string text) {
  editing_ = std::move(text);
  const int n = static_cast<int>(editing_.size());
  cursor_ = std::clamp(cursor_, 0, n);
  anchor_ = std::clamp(anchor_, 0, n);
  text_input_->Refresh();
  if (delayed_) {
    pending_commit_ = true;
  } else {
    CommitText(EncodeUtf8(editing_));
  }
}

void TextField::SelectAll() {
  anchor_ = 0;
  cursor_ = static_cast<int>(editing_.size());
  MarkDirtyRepaint();
}

void TextField::SelectRange(int anchor, int cursor) {
  const int n = static_cast<int>(editing_.size());
  anchor_ = std::clamp(anchor, 0, n);
  cursor_ = std::clamp(cursor, 0, n);
  MarkDirtyRepaint();
}

void TextField::DeleteSelection() {
  if (anchor_ == cursor_) return;
  const int a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
  std::u32string text = editing_;
  text.erase(a, b - a);
  cursor_ = anchor_ = a;
  ChangeText(std::move(text));
}

void TextField::Insert(std::u32string_view insert) {
  if (read_only_) return;
  std::u32string filtered;
  for (char32_t c : insert) {
    if (c == '\r') continue;
    if (c == '\n' && !multiline_) continue;
    if (c != '\n' && !AcceptsCharacter(c)) continue;
    filtered.push_back(c);
  }
  const int a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
  std::u32string text = editing_;
  text.erase(a, b - a);
  if (max_length_ >= 0) {
    const int room = max_length_ - static_cast<int>(text.size());
    if (room <= 0) return;
    if (static_cast<int>(filtered.size()) > room) filtered.resize(room);
  }
  text.insert(a, filtered);
  cursor_ = anchor_ = a + static_cast<int>(filtered.size());
  ChangeText(std::move(text));
}

int TextField::IndexAt(glm::vec2 world) const {
  TextSettings settings = text_input_->MakeTextSettings();
  if (!settings.font) return 0;
  settings.rich_text = false;
  const Rect content = text_input_->contentRect();
  const std::string display =
      password_ ? EncodeUtf8(std::u32string(editing_.size(), mask_char_)) : EncodeUtf8(editing_);
  TextLayout layout = LayoutText(display, settings, multiline_ ? content.width
                                                               : std::numeric_limits<float>::infinity());
  glm::vec2 local = text_input_->WorldToLocal(world) - text_input_->TextOrigin(layout);
  local.x += text_input_->scroll_x;
  if (layout.carets.empty()) return 0;
  // Line under the point, then the nearest caret on it.
  int line = static_cast<int>(std::floor(local.y / std::max(1.0f, layout.line_height)));
  line = std::clamp(line, 0, layout.caret_lines.back());
  int best = 0;
  float best_distance = std::numeric_limits<float>::max();
  for (size_t i = 0; i < layout.carets.size(); i++) {
    if (layout.caret_lines[i] != line) continue;
    const float d = std::abs(layout.carets[i].x - local.x);
    if (d < best_distance) {
      best_distance = d;
      best = static_cast<int>(i);
    }
  }
  return best;
}

void TextField::EnsureCursorVisible() {
  if (multiline_) return;
  TextSettings settings = text_input_->MakeTextSettings();
  if (!settings.font) return;
  settings.rich_text = false;
  const std::string display =
      password_ ? EncodeUtf8(std::u32string(editing_.size(), mask_char_)) : EncodeUtf8(editing_);
  TextLayout layout = LayoutText(display, settings);
  if (cursor_ >= static_cast<int>(layout.carets.size())) return;
  const float x = layout.carets[cursor_].x;
  const float width = text_input_->contentRect().width;
  float& scroll = text_input_->scroll_x;
  if (x - scroll > width - 2) scroll = x - width + 2;
  if (x - scroll < 0) scroll = x;
  scroll = std::clamp(scroll, 0.0f, std::max(0.0f, layout.size.x - width + 2));
}

void TextField::DrawCaretAndSelection(MeshGenerationContext& ctx) {
  TextSettings settings = text_input_->MakeTextSettings();
  if (!settings.font) return;
  const bool placeholder = editing_.empty() && !IsFocused() && !placeholder_.empty();
  const std::string display = DisplayText();
  const Rect content = text_input_->contentRect();
  settings.rich_text = false;
  if (placeholder) settings.color.rgba.a *= 0.5f;
  const float box = multiline_ ? content.width : std::numeric_limits<float>::infinity();
  TextLayout layout = LayoutText(display, settings, box);
  glm::vec2 origin = text_input_->TextOrigin(layout);
  origin.x -= text_input_->scroll_x;

  const bool focused = IsFocused();
  if (focused && anchor_ != cursor_ && !layout.carets.empty()) {
    Color selection(0.24f, 0.5f, 0.87f, 0.65f);
    text_input_->TryGetCustomStyle("--unity-selection-color", selection);
    const int a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
    for (int i = a; i < b && i + 1 < static_cast<int>(layout.carets.size()); i++) {
      const glm::vec2 p0 = layout.carets[i];
      glm::vec2 p1 = layout.carets[i + 1];
      if (layout.caret_lines[i] != layout.caret_lines[i + 1]) p1.x = p0.x + 4;
      ctx.DrawRect({origin.x + p0.x, origin.y + p0.y, p1.x - p0.x, layout.line_height},
                   selection);
    }
  }
  if (!display.empty()) {
    ComputedStyle style = text_input_->resolvedStyle();
    if (placeholder) style.color.rgba.a *= 0.5f;
    TextSettings draw = settings;
    draw.color = style.color;
    TextLayout drawn = LayoutText(display, draw, box);
    ctx.DrawText(drawn, *settings.font, origin, style);
  }
  if (focused && !read_only_ && panel()) {
    const double t = panel()->time() - blink_start_;
    if (std::fmod(t, 1.0) < 0.5 && cursor_ < static_cast<int>(layout.carets.size())) {
      Color caret = text_input_->resolvedStyle().color;
      text_input_->TryGetCustomStyle("--unity-cursor-color", caret);
      const glm::vec2 p = layout.carets[cursor_];
      ctx.DrawRect({origin.x + p.x, origin.y + p.y, 1.0f, layout.line_height}, caret);
    }
  }
}

void TextField::HandleEventBubbleUp(EventBase& event) {
  if (Is<FocusEvent>(event)) {
    blink_start_ = panel() ? panel()->time() : 0.0;
    if (!dragging_) SelectAll();
    if (panel()) panel()->OnTextInputFocus(true);
    // Blink the caret.
    Schedule([this] { MarkDirtyRepaint(); }).Every(250).Until([this] { return !IsFocused(); });
    text_input_->Refresh();
    return;
  }
  if (Is<BlurEvent>(event)) {
    if (pending_commit_) {
      pending_commit_ = false;
      CommitText(EncodeUtf8(editing_));
    }
    anchor_ = cursor_;
    text_input_->scroll_x = 0.0f;
    if (panel()) panel()->OnTextInputFocus(false);
    text_input_->Refresh();
    return;
  }
  if (!enabledInHierarchy()) return;

  if (Is<PointerDownEvent>(event)) {
    auto& e = static_cast<PointerDownEvent&>(event);
    if (e.button != 0) return;
    const int index = IndexAt(e.position);
    if (e.clickCount == 2) {
      int a = index, b = index;
      while (a > 0 && IsWordChar(editing_[a - 1])) a--;
      while (b < static_cast<int>(editing_.size()) && IsWordChar(editing_[b])) b++;
      SelectRange(a, b);
    } else if (e.clickCount >= 3) {
      SelectAll();
    } else {
      cursor_ = index;
      if (!e.shiftKey()) anchor_ = index;
      dragging_ = true;
      CapturePointer(e.pointerId);
    }
    blink_start_ = panel() ? panel()->time() : 0.0;
    MarkDirtyRepaint();
    event.StopPropagation();
    return;
  }
  if (Is<PointerMoveEvent>(event) && dragging_) {
    cursor_ = IndexAt(static_cast<PointerMoveEvent&>(event).position);
    EnsureCursorVisible();
    MarkDirtyRepaint();
    event.StopPropagation();
    return;
  }
  if (Is<PointerUpEvent>(event) && dragging_) {
    dragging_ = false;
    ReleasePointer(static_cast<PointerUpEvent&>(event).pointerId);
    event.StopPropagation();
    return;
  }
  if (!Is<KeyDownEvent>(event) || !IsFocused()) return;

  auto& e = static_cast<KeyDownEvent&>(event);
  const int n = static_cast<int>(editing_.size());
  const bool shift = e.shiftKey();
  bool handled = true;
  auto move_to = [&](int index) {
    cursor_ = std::clamp(index, 0, n);
    if (!shift) anchor_ = cursor_;
  };
  auto word_left = [&](int i) {
    while (i > 0 && !IsWordChar(editing_[i - 1])) i--;
    while (i > 0 && IsWordChar(editing_[i - 1])) i--;
    return i;
  };
  auto word_right = [&](int i) {
    while (i < n && !IsWordChar(editing_[i])) i++;
    while (i < n && IsWordChar(editing_[i])) i++;
    return i;
  };

  if (e.character != 0 && !e.actionKey()) {
    if (read_only_) return;
    Insert(std::u32string(1, e.character));
  } else if (e.actionKey() && e.keyCode == KeyCode::kA) {
    SelectAll();
  } else if (e.actionKey() && (e.keyCode == KeyCode::kC || e.keyCode == KeyCode::kX)) {
    if (anchor_ != cursor_ && !password_ && panel() && panel()->clipboard.set) {
      const int a = std::min(anchor_, cursor_), b = std::max(anchor_, cursor_);
      panel()->clipboard.set(EncodeUtf8(editing_.substr(a, b - a)));
      if (e.keyCode == KeyCode::kX && !read_only_) DeleteSelection();
    }
  } else if (e.actionKey() && e.keyCode == KeyCode::kV) {
    if (panel() && panel()->clipboard.get && !read_only_) {
      Insert(DecodeUtf8(panel()->clipboard.get()));
    }
  } else {
    switch (e.keyCode) {
      case KeyCode::kLeft:
        if (!shift && anchor_ != cursor_) {
          move_to(std::min(anchor_, cursor_));
        } else {
          move_to(e.actionKey() ? word_left(cursor_) : cursor_ - 1);
        }
        break;
      case KeyCode::kRight:
        if (!shift && anchor_ != cursor_) {
          move_to(std::max(anchor_, cursor_));
        } else {
          move_to(e.actionKey() ? word_right(cursor_) : cursor_ + 1);
        }
        break;
      case KeyCode::kHome: {
        int i = cursor_;
        if (e.actionKey()) {
          i = 0;
        } else {
          while (i > 0 && editing_[i - 1] != '\n') i--;
        }
        move_to(i);
        break;
      }
      case KeyCode::kEnd: {
        int i = cursor_;
        if (e.actionKey()) {
          i = n;
        } else {
          while (i < n && editing_[i] != '\n') i++;
        }
        move_to(i);
        break;
      }
      case KeyCode::kUp:
      case KeyCode::kDown: {
        if (!multiline_) {
          handled = false;
          break;
        }
        // Same column on the previous/next line.
        int start = cursor_;
        while (start > 0 && editing_[start - 1] != '\n') start--;
        const int column = cursor_ - start;
        if (e.keyCode == KeyCode::kUp) {
          if (start == 0) {
            move_to(0);
            break;
          }
          int prev = start - 1;
          while (prev > 0 && editing_[prev - 1] != '\n') prev--;
          move_to(std::min(prev + column, start - 1));
        } else {
          int next = cursor_;
          while (next < n && editing_[next] != '\n') next++;
          if (next >= n) {
            move_to(n);
            break;
          }
          next++;
          int end = next;
          while (end < n && editing_[end] != '\n') end++;
          move_to(std::min(next + column, end));
        }
        break;
      }
      case KeyCode::kBackspace:
        if (read_only_) break;
        if (anchor_ == cursor_ && cursor_ > 0) {
          anchor_ = e.actionKey() ? word_left(cursor_) : cursor_ - 1;
        }
        DeleteSelection();
        break;
      case KeyCode::kDelete:
        if (read_only_) break;
        if (anchor_ == cursor_ && cursor_ < n) {
          anchor_ = e.actionKey() ? word_right(cursor_) : cursor_ + 1;
        }
        DeleteSelection();
        break;
      case KeyCode::kEnter:
      case KeyCode::kKeypadEnter:
        if (multiline_ && !e.actionKey()) {
          Insert(U"\n");
        } else if (pending_commit_) {
          pending_commit_ = false;
          CommitText(EncodeUtf8(editing_));
        }
        break;
      case KeyCode::kEscape:
        Blur();
        break;
      default: handled = false; break;
    }
  }
  if (handled) {
    blink_start_ = panel() ? panel()->time() : 0.0;
    EnsureCursorVisible();
    MarkDirtyRepaint();
    event.StopPropagation();
  }
}

bool TextField::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value" || name == "text") {
    SetValue(VariantToString(value));
  } else if (name == "label") {
    SetLabel(VariantToString(value));
  } else if (name == "max-length") {
    if (auto i = VariantToInt(value)) SetMaxLength(static_cast<int>(*i));
  } else if (name == "multiline") {
    if (auto b = VariantToBool(value)) SetMultiline(*b);
  } else if (name == "password") {
    if (auto b = VariantToBool(value)) SetIsPasswordField(*b);
  } else if (name == "mask-character") {
    const auto text = DecodeUtf8(VariantToString(value));
    if (!text.empty()) SetMaskChar(text[0]);
  } else if (name == "readonly" || name == "is-read-only") {
    if (auto b = VariantToBool(value)) SetIsReadOnly(*b);
  } else if (name == "is-delayed") {
    if (auto b = VariantToBool(value)) SetIsDelayed(*b);
  } else if (name == "placeholder-text") {
    SetPlaceholder(VariantToString(value));
  } else {
    return VisualElement::SetProperty(name, value);
  }
  return true;
}

std::optional<mtext::Variant> TextField::GetProperty(std::string_view name) const {
  if (name == "value" || name == "text") return value_;
  if (name == "label") return label();
  if (name == "max-length") return max_length_;
  if (name == "multiline") return multiline_;
  if (name == "password") return password_;
  if (name == "readonly" || name == "is-read-only") return read_only_;
  if (name == "is-delayed") return delayed_;
  if (name == "placeholder-text") return placeholder_;
  return VisualElement::GetProperty(name);
}

IntegerField::IntegerField() {
  AddToClassList(ussClassName);
  SetIsDelayed(true);
  SetValueWithoutNotify("0");
}

void IntegerField::SetIntValue(int value) {
  const int previous = int_value_;
  int_value_ = value;
  SetValueWithoutNotify(std::to_string(value));
  if (previous != value) {
    ChangeEvent<int> event(previous, value);
    SendEvent(event);
  }
}

void IntegerField::CommitText(const std::string& text) {
  char* end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str()) {
    SetValueWithoutNotify(std::to_string(int_value_));
    return;
  }
  SetIntValue(static_cast<int>(parsed));
}

bool IntegerField::AcceptsCharacter(char32_t c) const {
  return (c >= '0' && c <= '9') || c == '-' || c == '+';
}

bool IntegerField::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value") {
    if (auto i = VariantToInt(value)) SetIntValue(static_cast<int>(*i));
    return true;
  }
  return TextField::SetProperty(name, value);
}

std::optional<mtext::Variant> IntegerField::GetProperty(std::string_view name) const {
  if (name == "value") return int_value_;
  return TextField::GetProperty(name);
}

FloatField::FloatField() {
  AddToClassList(ussClassName);
  SetIsDelayed(true);
  SetValueWithoutNotify("0");
}

void FloatField::SetFloatValue(float value) {
  const float previous = float_value_;
  float_value_ = value;
  SetValueWithoutNotify(FormatFloat(value));
  if (previous != value) {
    ChangeEvent<float> event(previous, value);
    SendEvent(event);
  }
}

void FloatField::CommitText(const std::string& text) {
  char* end = nullptr;
  const float parsed = std::strtof(text.c_str(), &end);
  if (end == text.c_str()) {
    SetValueWithoutNotify(FormatFloat(float_value_));
    return;
  }
  SetFloatValue(parsed);
}

bool FloatField::AcceptsCharacter(char32_t c) const {
  return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' ||
         c == 'E';
}

bool FloatField::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "value") {
    if (auto f = VariantToFloat(value)) SetFloatValue(static_cast<float>(*f));
    return true;
  }
  return TextField::SetProperty(name, value);
}

std::optional<mtext::Variant> FloatField::GetProperty(std::string_view name) const {
  if (name == "value") return mtext::Variant(float_value_);
  return TextField::GetProperty(name);
}

// --------------------------------------------------------------- Scroller --

Scroller::Scroller(SliderDirection direction) : direction_(direction) {
  AddToClassList(ussClassName);
  AddToClassList(direction == SliderDirection::kVertical ? "unity-scroller--vertical"
                                                         : "unity-scroller--horizontal");
  auto tracker = std::make_unique<VisualElement>();
  tracker->AddToClassList("unity-scroller__slider");
  tracker_ = hierarchy().Add(std::move(tracker));
  auto dragger = std::make_unique<VisualElement>();
  dragger->AddToClassList("unity-scroller__dragger");
  // Structure the scroller needs whatever the theme says.
  dragger->style().position = Position::kAbsolute;
  if (direction == SliderDirection::kVertical) {
    dragger->style().left = dragger->style().right = Length::Px(0);
  } else {
    dragger->style().top = dragger->style().bottom = Length::Px(0);
    style().flexDirection = FlexDirection::kRow;
  }
  tracker_->style().flexGrow = 1.0f;
  dragger_ = tracker_->Add(std::move(dragger));
  UpdateDragger();
}

void Scroller::SetValue(float value) {
  value = std::clamp(value, 0.0f, std::max(0.0f, high_));
  if (value == value_) return;
  value_ = value;
  UpdateDragger();
  valueChanged(value_);
}

void Scroller::SetHighValue(float high) {
  high_ = std::max(0.0f, high);
  value_ = std::min(value_, high_);
  UpdateDragger();
}

void Scroller::SetFactor(float factor) {
  factor_ = std::clamp(factor, 0.05f, 1.0f);
  UpdateDragger();
}

void Scroller::UpdateDragger() {
  const float t = high_ > 0 ? value_ / high_ : 0.0f;
  auto& s = dragger_->style();
  const float size = factor_ * 100.0f;
  const float pos = t * (100.0f - size);
  if (direction_ == SliderDirection::kVertical) {
    s.height = Length::Percent(size);
    s.top = Length::Percent(pos);
  } else {
    s.width = Length::Percent(size);
    s.left = Length::Percent(pos);
  }
}

void Scroller::HandleEventBubbleUp(EventBase& event) {
  const bool vertical = direction_ == SliderDirection::kVertical;
  auto axis = [vertical](glm::vec2 v) { return vertical ? v.y : v.x; };
  auto track_length = [&] {
    const Rect& t = tracker_->layout();
    const Rect& d = dragger_->layout();
    return std::max(1.0f, vertical ? t.height - d.height : t.width - d.width);
  };
  if (Is<PointerDownEvent>(event)) {
    auto& e = static_cast<PointerDownEvent&>(event);
    if (e.button != 0) return;
    const glm::vec2 local = tracker_->WorldToLocal(e.position);
    const Rect& d = dragger_->layout();
    const float dragger_pos = vertical ? d.y : d.x;
    const float dragger_size = vertical ? d.height : d.width;
    if (axis(local) < dragger_pos || axis(local) > dragger_pos + dragger_size) {
      // Clicked the track: jump so the dragger centers on the pointer.
      SetValue((axis(local) - dragger_size / 2) / track_length() * high_);
      drag_offset_ = dragger_size / 2;
    } else {
      drag_offset_ = axis(local) - dragger_pos;
    }
    dragging_ = true;
    CapturePointer(e.pointerId);
    event.StopPropagation();
  } else if (Is<PointerMoveEvent>(event) && dragging_) {
    auto& e = static_cast<PointerMoveEvent&>(event);
    const glm::vec2 local = tracker_->WorldToLocal(e.position);
    SetValue((axis(local) - drag_offset_) / track_length() * high_);
    event.StopPropagation();
  } else if (Is<PointerUpEvent>(event) && dragging_) {
    dragging_ = false;
    ReleasePointer(static_cast<PointerUpEvent&>(event).pointerId);
    event.StopPropagation();
  } else if (Is<PointerCaptureOutEvent>(event)) {
    dragging_ = false;
  }
}

// ------------------------------------------------------------- ScrollView --

ScrollView::ScrollView(ScrollViewMode mode) : mode_(mode) {
  AddToClassList(ussClassName);
  // The structure is set here so a ScrollView works with any theme; the
  // theme only styles it.
  auto row = std::make_unique<VisualElement>();
  row->AddToClassList("unity-scroll-view__content-and-vertical-scroll-container");
  row->style().flexDirection = FlexDirection::kRow;
  row->style().flexGrow = 1.0f;
  row->style().flexShrink = 1.0f;
  VisualElement* r = hierarchy().Add(std::move(row));
  auto viewport = std::make_unique<VisualElement>();
  viewport->AddToClassList("unity-scroll-view__content-viewport");
  viewport->style().flexGrow = 1.0f;
  viewport->style().flexShrink = 1.0f;
  viewport->style().overflow = Overflow::kHidden;
  viewport_ = r->Add(std::move(viewport));
  auto content = std::make_unique<VisualElement>();
  content->AddToClassList("unity-scroll-view__content-container");
  content->style().position = Position::kAbsolute;
  content->style().left = content->style().top = Length::Px(0);
  content_ = viewport_->Add(std::move(content));
  vertical_ = static_cast<Scroller*>(
      r->Add(std::make_unique<Scroller>(SliderDirection::kVertical)));
  horizontal_ = static_cast<Scroller*>(
      hierarchy().Add(std::make_unique<Scroller>(SliderDirection::kHorizontal)));
  vertical_->valueChanged += [this](float v) { SetScrollOffset({offset_.x, v}); };
  horizontal_->valueChanged += [this](float v) { SetScrollOffset({v, offset_.y}); };
  auto refresh = [this](GeometryChangedEvent&) { UpdateScrollers(); };
  viewport_->RegisterCallback<GeometryChangedEvent>(refresh);
  content_->RegisterCallback<GeometryChangedEvent>(refresh);
  SetMode(mode);
}

void ScrollView::SetMode(ScrollViewMode mode) {
  mode_ = mode;
  EnableInClassList("unity-scroll-view--vertical", mode == ScrollViewMode::kVertical);
  EnableInClassList("unity-scroll-view--horizontal", mode == ScrollViewMode::kHorizontal);
  EnableInClassList("unity-scroll-view--vertical-horizontal",
                    mode == ScrollViewMode::kVerticalAndHorizontal);
  // Vertical content is as wide as the view; horizontal content as tall.
  auto& content = content_->style();
  content.right = mode == ScrollViewMode::kVertical ? std::optional(Length::Px(0)) : std::nullopt;
  content.bottom =
      mode == ScrollViewMode::kHorizontal ? std::optional(Length::Px(0)) : std::nullopt;
  content.flexDirection = mode == ScrollViewMode::kHorizontal
                              ? std::optional(FlexDirection::kRow)
                              : std::nullopt;
  UpdateScrollers();
}

void ScrollView::SetVerticalScrollerVisibility(ScrollerVisibility v) {
  vertical_visibility_ = v;
  UpdateScrollers();
}

void ScrollView::SetHorizontalScrollerVisibility(ScrollerVisibility v) {
  horizontal_visibility_ = v;
  UpdateScrollers();
}

void ScrollView::UpdateScrollers() {
  const Rect& view = viewport_->layout();
  const Rect& content = content_->layout();
  const glm::vec2 overflow(std::max(0.0f, content.width - view.width),
                           std::max(0.0f, content.height - view.height));
  auto show = [](ScrollerVisibility v, bool needed) {
    return v == ScrollerVisibility::kAlwaysVisible ||
           (v == ScrollerVisibility::kAuto && needed);
  };
  const bool can_v = mode_ != ScrollViewMode::kHorizontal;
  const bool can_h = mode_ != ScrollViewMode::kVertical;
  const bool show_v = can_v && show(vertical_visibility_, overflow.y > 0.5f);
  const bool show_h = can_h && show(horizontal_visibility_, overflow.x > 0.5f);
  vertical_->style().display = show_v ? DisplayStyle::kFlex : DisplayStyle::kNone;
  horizontal_->style().display = show_h ? DisplayStyle::kFlex : DisplayStyle::kNone;
  vertical_->SetHighValue(overflow.y);
  horizontal_->SetHighValue(overflow.x);
  vertical_->SetFactor(content.height > 0 ? view.height / content.height : 1.0f);
  horizontal_->SetFactor(content.width > 0 ? view.width / content.width : 1.0f);
  SetScrollOffset(offset_);
}

void ScrollView::SetScrollOffset(glm::vec2 offset) {
  const Rect& view = viewport_->layout();
  const Rect& content = content_->layout();
  const glm::vec2 max_offset(std::max(0.0f, content.width - view.width),
                             std::max(0.0f, content.height - view.height));
  offset = glm::clamp(offset, glm::vec2(0.0f), max_offset);
  if (mode_ == ScrollViewMode::kVertical) offset.x = 0.0f;
  if (mode_ == ScrollViewMode::kHorizontal) offset.y = 0.0f;
  offset_ = offset;
  viewport_->SetChildrenOffset(-offset_);
  vertical_->SetValue(offset_.y);
  horizontal_->SetValue(offset_.x);
}

void ScrollView::ScrollTo(VisualElement* child) {
  if (!child || !content_->Contains(child)) return;
  const glm::vec2 top_left = content_->WorldToLocal(child->LocalToWorld({0, 0}));
  const Rect& view = viewport_->layout();
  glm::vec2 offset = offset_;
  const Rect& r = child->layout();
  if (top_left.y < offset.y) offset.y = top_left.y;
  if (top_left.y + r.height > offset.y + view.height) offset.y = top_left.y + r.height - view.height;
  if (top_left.x < offset.x) offset.x = top_left.x;
  if (top_left.x + r.width > offset.x + view.width) offset.x = top_left.x + r.width - view.width;
  SetScrollOffset(offset);
}

void ScrollView::HandleEventBubbleUp(EventBase& event) {
  if (!Is<WheelEvent>(event)) return;
  auto& e = static_cast<WheelEvent&>(event);
  const glm::vec2 before = offset_;
  glm::vec2 delta = e.delta * wheel_size_;
  if (mode_ == ScrollViewMode::kHorizontal && delta.x == 0.0f) delta.x = delta.y;
  SetScrollOffset(offset_ + delta);
  if (offset_ != before) event.StopPropagation();
}

bool ScrollView::SetProperty(std::string_view name, const mtext::Variant& value) {
  auto visibility = [](const std::string& v) {
    return v == "AlwaysVisible" ? ScrollerVisibility::kAlwaysVisible
           : v == "Hidden"      ? ScrollerVisibility::kHidden
                                : ScrollerVisibility::kAuto;
  };
  if (name == "mode") {
    const std::string mode = VariantToString(value);
    SetMode(mode == "Horizontal"              ? ScrollViewMode::kHorizontal
            : mode == "VerticalAndHorizontal" ? ScrollViewMode::kVerticalAndHorizontal
                                              : ScrollViewMode::kVertical);
  } else if (name == "vertical-scroller-visibility") {
    SetVerticalScrollerVisibility(visibility(VariantToString(value)));
  } else if (name == "horizontal-scroller-visibility") {
    SetHorizontalScrollerVisibility(visibility(VariantToString(value)));
  } else if (name == "mouse-wheel-scroll-size") {
    if (auto f = VariantToFloat(value)) SetMouseWheelScrollSize(static_cast<float>(*f));
  } else {
    return VisualElement::SetProperty(name, value);
  }
  return true;
}

std::optional<mtext::Variant> ScrollView::GetProperty(std::string_view name) const {
  if (name == "mode") {
    return std::string(mode_ == ScrollViewMode::kHorizontal ? "Horizontal"
                       : mode_ == ScrollViewMode::kVerticalAndHorizontal
                           ? "VerticalAndHorizontal"
                           : "Vertical");
  }
  if (name == "mouse-wheel-scroll-size") return mtext::Variant(wheel_size_);
  return VisualElement::GetProperty(name);
}

// ---------------------------------------------------------------- Foldout --

Foldout::Foldout() {
  AddToClassList(ussClassName);
  auto toggle = std::make_unique<Toggle>();
  toggle->AddToClassList("unity-foldout__toggle");
  toggle_ = static_cast<Toggle*>(hierarchy().Add(std::move(toggle)));
  auto content = std::make_unique<VisualElement>();
  content->AddToClassList("unity-foldout__content");
  content_ = hierarchy().Add(std::move(content));
  toggle_->RegisterValueChangedCallback<bool>([this](ChangeEvent<bool>& e) {
    if (e.target() != toggle_) return;
    content_->style().display = e.newValue ? DisplayStyle::kFlex : DisplayStyle::kNone;
    SetPseudoState(kPseudoChecked, e.newValue);
  });
  toggle_->SetValueWithoutNotify(true);
  SetPseudoState(kPseudoChecked, true);
}

const std::string& Foldout::text() const { return toggle_->text(); }
void Foldout::SetText(std::string text) { toggle_->SetText(std::move(text)); }
bool Foldout::value() const { return toggle_->value(); }
void Foldout::SetValue(bool open) {
  toggle_->SetValue(open);
  content_->style().display = open ? DisplayStyle::kFlex : DisplayStyle::kNone;
  SetPseudoState(kPseudoChecked, open);
}

bool Foldout::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "text") {
    SetText(VariantToString(value));
  } else if (name == "value") {
    if (auto b = VariantToBool(value)) SetValue(*b);
  } else {
    return VisualElement::SetProperty(name, value);
  }
  return true;
}

std::optional<mtext::Variant> Foldout::GetProperty(std::string_view name) const {
  if (name == "text") return text();
  if (name == "value") return value();
  return VisualElement::GetProperty(name);
}

// --------------------------------------------------------------- ListView --

ListView::ListView() {
  AddToClassList(ussClassName);
  AddToClassList("unity-collection-view");
  SetFocusable(true);
  auto scroll = std::make_unique<ScrollView>();
  scroll->AddToClassList("unity-list-view__scroll-view");
  scroll->AddToClassList("unity-collection-view__scroll-view");
  scroll->style().flexGrow = 1.0f;
  scroll_ = static_cast<ScrollView*>(hierarchy().Add(std::move(scroll)));
}

void ListView::SetItemCount(int count) {
  count_ = std::max(0, count);
  std::erase_if(selected_, [this](int i) { return i >= count_; });
  RefreshItems();
}

void ListView::SetFixedItemHeight(float height) {
  item_height_ = height;
  for (VisualElement* item : items_) item->style().height = Length::Px(item_height_);
}

void ListView::RefreshItems() {
  for (size_t i = 0; i < items_.size(); i++) {
    if (unbindItem) unbindItem(items_[i], static_cast<int>(i));
  }
  scroll_->Clear();
  items_.clear();
  for (int i = 0; i < count_; i++) {
    std::unique_ptr<VisualElement> item = makeItem ? makeItem() : std::make_unique<Label>();
    item->AddToClassList("unity-list-view__item");
    item->AddToClassList("unity-collection-view__item");
    item->EnableInClassList("unity-collection-view__item--alternative-background", i % 2);
    item->style().height = Length::Px(item_height_);
    item->style().flexShrink = 0.0f;
    VisualElement* raw = scroll_->Add(std::move(item));
    items_.push_back(raw);
    if (bindItem) {
      bindItem(raw, i);
    } else if (auto* label = dynamic_cast<Label*>(raw)) {
      label->SetText(std::to_string(i));
    }
  }
  UpdateSelectionClasses();
}

void ListView::UpdateSelectionClasses() {
  for (size_t i = 0; i < items_.size(); i++) {
    const bool selected =
        std::find(selected_.begin(), selected_.end(), static_cast<int>(i)) != selected_.end();
    items_[i]->EnableInClassList("unity-collection-view__item--selected", selected);
    items_[i]->SetPseudoState(kPseudoChecked, selected);
  }
}

void ListView::SetSelection(int index) {
  if (selection_type_ == SelectionType::kNone) return;
  std::vector<int> next;
  if (index >= 0 && index < count_) next.push_back(index);
  if (next == selected_) return;
  selected_ = next;
  UpdateSelectionClasses();
  if (index >= 0 && index < static_cast<int>(items_.size())) scroll_->ScrollTo(items_[index]);
  selectionChanged(selected_);
}

void ListView::AddToSelection(int index) {
  if (selection_type_ != SelectionType::kMultiple) {
    SetSelection(index);
    return;
  }
  if (index < 0 || index >= count_ ||
      std::find(selected_.begin(), selected_.end(), index) != selected_.end()) {
    return;
  }
  selected_.push_back(index);
  UpdateSelectionClasses();
  selectionChanged(selected_);
}

void ListView::ClearSelection() {
  if (selected_.empty()) return;
  selected_.clear();
  UpdateSelectionClasses();
  selectionChanged(selected_);
}

void ListView::HandleEventBubbleUp(EventBase& event) {
  if (!enabledInHierarchy()) return;
  if (Is<PointerDownEvent>(event)) {
    auto& e = static_cast<PointerDownEvent&>(event);
    if (e.button != 0) return;
    for (size_t i = 0; i < items_.size(); i++) {
      if (!items_[i]->Contains(e.target())) continue;
      const int index = static_cast<int>(i);
      if (e.ctrlKey() && selection_type_ == SelectionType::kMultiple) {
        auto it = std::find(selected_.begin(), selected_.end(), index);
        if (it != selected_.end()) {
          selected_.erase(it);
          UpdateSelectionClasses();
          selectionChanged(selected_);
        } else {
          AddToSelection(index);
        }
      } else {
        SetSelection(index);
      }
      if (e.clickCount == 2) itemsChosen(selected_);
      break;
    }
  } else if (Is<NavigationMoveEvent>(event)) {
    auto& e = static_cast<NavigationMoveEvent&>(event);
    int dir = e.direction == NavigationDirection::kUp     ? -1
              : e.direction == NavigationDirection::kDown ? 1
                                                          : 0;
    if (dir == 0 || count_ == 0) return;
    SetSelection(std::clamp(selectedIndex() + dir, 0, count_ - 1));
    event.StopPropagation();
  } else if (Is<NavigationSubmitEvent>(event)) {
    if (!selected_.empty()) itemsChosen(selected_);
  }
}

bool ListView::SetProperty(std::string_view name, const mtext::Variant& value) {
  if (name == "fixed-item-height") {
    if (auto f = VariantToFloat(value)) SetFixedItemHeight(static_cast<float>(*f));
  } else if (name == "selection-type") {
    const std::string t = VariantToString(value);
    SetSelectionType(t == "None"       ? SelectionType::kNone
                     : t == "Multiple" ? SelectionType::kMultiple
                                       : SelectionType::kSingle);
  } else if (name == "item-count") {
    if (auto i = VariantToInt(value)) SetItemCount(static_cast<int>(*i));
  } else if (name == "selected-index") {
    if (auto i = VariantToInt(value)) SetSelection(static_cast<int>(*i));
  } else {
    return VisualElement::SetProperty(name, value);
  }
  return true;
}

std::optional<mtext::Variant> ListView::GetProperty(std::string_view name) const {
  if (name == "fixed-item-height") return mtext::Variant(item_height_);
  if (name == "item-count") return count_;
  if (name == "selected-index") return selectedIndex();
  if (name == "selection-type") {
    return std::string(selection_type_ == SelectionType::kNone       ? "None"
                       : selection_type_ == SelectionType::kMultiple ? "Multiple"
                                                                     : "Single");
  }
  return VisualElement::GetProperty(name);
}

// ----------------------------------------------------------- Registration --

void RegisterBuiltinControls(UxmlFactoryRegistry& registry) {
  using F = FieldType;
  auto base = VisualElementAttributes();
  auto with = [&base](std::vector<UxmlAttributeDescription> extra) {
    std::vector<UxmlAttributeDescription> all = base;
    all.insert(all.end(), extra.begin(), extra.end());
    return all;
  };
  auto add = [&registry](const char* name, const char* category, auto create,
                         std::vector<UxmlAttributeDescription> attributes) {
    registry.Register(UxmlTypeInfo{name, category, create, std::move(attributes)});
  };
  const UxmlAttributeDescription text{"text", F::kString, "", {}, "", "Text to show"};
  const UxmlAttributeDescription label{"label", F::kString, "", {}, "", "Text before the field"};
  const UxmlAttributeDescription rich{"enable-rich-text", F::kBool, "true", {}, "",
                                      "Allow <b>, <i>, <color> and <size> tags"};

  add("VisualElement", "Containers", [] { return std::make_unique<VisualElement>(); }, base);
  add("ScrollView", "Containers", [] { return std::make_unique<ScrollView>(); },
      with({{"mode", F::kEnum, "Vertical", {"Vertical", "Horizontal", "VerticalAndHorizontal"}},
            {"vertical-scroller-visibility", F::kEnum, "Auto", {"Auto", "AlwaysVisible", "Hidden"}},
            {"horizontal-scroller-visibility", F::kEnum, "Auto", {"Auto", "AlwaysVisible", "Hidden"}},
            {"mouse-wheel-scroll-size", F::kFloat, "18"}}));
  add("ListView", "Containers", [] { return std::make_unique<ListView>(); },
      with({{"fixed-item-height", F::kFloat, "22"},
            {"selection-type", F::kEnum, "Single", {"None", "Single", "Multiple"}},
            {"item-count", F::kInt, "0"}}));
  add("GroupBox", "Containers", [] { return std::make_unique<GroupBox>(); }, with({text}));
  add("Foldout", "Containers", [] { return std::make_unique<Foldout>(); },
      with({text, {"value", F::kBool, "true", {}, "", "Open"}}));
  add("Box", "Containers", [] { return std::make_unique<Box>(); }, base);
  add("TemplateContainer", "Containers", [] { return std::make_unique<TemplateContainer>(); },
      base);

  add("Label", "Controls", [] { return std::make_unique<Label>(); }, with({text, rich}));
  add("TextElement", "Controls", [] { return std::make_unique<TextElement>(); },
      with({text, rich}));
  add("Button", "Controls", [] { return std::make_unique<Button>(); }, with({text, rich}));
  add("RepeatButton", "Controls", [] { return std::make_unique<RepeatButton>(); },
      with({text}));
  add("Image", "Controls", [] { return std::make_unique<Image>(); },
      with({{"image", F::kAsset, "", {}, "", "Texture to show"},
            {"scale-mode", F::kEnum, "ScaleToFit", {"StretchToFill", "ScaleAndCrop", "ScaleToFit"}},
            {"tint-color", F::kColor, "#FFFFFF"}}));
  add("Toggle", "Controls", [] { return std::make_unique<Toggle>(); },
      with({label, text, {"value", F::kBool, "false"}}));
  add("RadioButton", "Controls", [] { return std::make_unique<RadioButton>(); },
      with({label, text, {"value", F::kBool, "false"}}));
  const std::vector<UxmlAttributeDescription> slider_attrs = {
      label,
      {"low-value", F::kFloat, "0"},
      {"high-value", F::kFloat, "10"},
      {"page-size", F::kFloat, "0"},
      {"direction", F::kEnum, "Horizontal", {"Horizontal", "Vertical"}},
      {"inverted", F::kBool, "false"}};
  auto slider = with(slider_attrs);
  slider.push_back({"value", F::kFloat, "0"});
  add("Slider", "Controls", [] { return std::make_unique<Slider>(); }, slider);
  auto slider_int = with(slider_attrs);
  slider_int.push_back({"value", F::kInt, "0"});
  add("SliderInt", "Controls", [] { return std::make_unique<SliderInt>(); }, slider_int);
  add("ProgressBar", "Controls", [] { return std::make_unique<ProgressBar>(); },
      with({{"low-value", F::kFloat, "0"},
            {"high-value", F::kFloat, "100"},
            {"value", F::kFloat, "0"},
            {"title", F::kString, ""}}));
  const std::vector<UxmlAttributeDescription> text_attrs = {
      label,
      {"max-length", F::kInt, "-1"},
      {"multiline", F::kBool, "false"},
      {"password", F::kBool, "false"},
      {"mask-character", F::kString, "*"},
      {"readonly", F::kBool, "false"},
      {"is-delayed", F::kBool, "false"},
      {"placeholder-text", F::kString, ""}};
  auto text_field = with(text_attrs);
  text_field.push_back({"value", F::kString, ""});
  add("TextField", "Controls", [] { return std::make_unique<TextField>(); }, text_field);
  auto int_field = with({label});
  int_field.push_back({"value", F::kInt, "0"});
  add("IntegerField", "Controls", [] { return std::make_unique<IntegerField>(); }, int_field);
  auto float_field = with({label});
  float_field.push_back({"value", F::kFloat, "0"});
  add("FloatField", "Controls", [] { return std::make_unique<FloatField>(); }, float_field);
  add("Scroller", "Controls", [] { return std::make_unique<Scroller>(); }, base);
}

}  // namespace Mercury::UI
