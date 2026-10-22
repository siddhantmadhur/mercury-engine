/*
 * controls.h
 *
 * The built-in controls, matching Unity UI Toolkit's names, structure and
 * USS class names (unity-button, unity-toggle__checkmark, ...) so style
 * sheets written for Unity carry over.
 *
 *   auto* play = root->Q<Button>("play");
 *   play->clicked += [] { StartGame(); };
 *
 *   auto* volume = root->Q<Slider>("volume");
 *   volume->RegisterValueChangedCallback<float>(
 *       [](ChangeEvent<float>& e) { SetVolume(e.newValue); });
 */
#ifndef MERCURY_UI_CONTROLS_H
#define MERCURY_UI_CONTROLS_H

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "ui/binding.h"
#include "ui/font.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

/**
 * @brief A list of handlers, used like C#'s event: button->clicked += fn;
 */
template <typename... Args>
class UIAction {
 public:
  UIAction& operator+=(std::function<void(Args...)> handler) {
    handlers_.push_back(std::move(handler));
    return *this;
  }
  void Clear() { handlers_.clear(); }
  bool empty() const { return handlers_.empty(); }
  void operator()(Args... args) const {
    // Copy: a handler may add or clear handlers.
    auto handlers = handlers_;
    for (auto& h : handlers) h(args...);
  }

 private:
  std::vector<std::function<void(Args...)>> handlers_;
};

// ----------------------------------------------------------------- Text ---

class TextElement : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-text-element";
  TextElement();
  explicit TextElement(std::string text) : TextElement() { SetText(std::move(text)); }
  const char* typeName() const override { return "TextElement"; }

  const std::string& text() const { return text_; }
  void SetText(std::string text);
  bool enableRichText() const { return rich_text_; }
  void SetEnableRichText(bool enable);

  // The text as drawn (masked for passwords).
  virtual std::string DisplayText() const { return text_; }
  TextSettings MakeTextSettings() const;
  Font* ResolveFont() const;

  bool HasMeasure() const override { return true; }
  glm::vec2 Measure(float width, MeasureMode width_mode, float height,
                    MeasureMode height_mode) override;
  void GenerateVisualContent(MeshGenerationContext& context) override;

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

  // Where text is drawn within the content rect, after alignment.
  glm::vec2 TextOrigin(const TextLayout& layout) const;

 private:
  std::string text_;
  bool rich_text_ = true;
};

class Label : public TextElement {
 public:
  static constexpr const char* ussClassName = "unity-label";
  Label();
  explicit Label(std::string text) : Label() { SetText(std::move(text)); }
  const char* typeName() const override { return "Label"; }
};

class Button : public TextElement {
 public:
  static constexpr const char* ussClassName = "unity-button";
  Button();
  explicit Button(std::string text) : Button() { SetText(std::move(text)); }
  explicit Button(std::function<void()> on_click, std::string text = "")
      : Button() {
    clicked += std::move(on_click);
    SetText(std::move(text));
  }
  const char* typeName() const override { return "Button"; }

  UIAction<> clicked;
  Clickable* clickable() const { return clickable_; }

 private:
  Clickable* clickable_ = nullptr;
};

// Fires repeatedly while held (Unity's RepeatButton).
class RepeatButton : public TextElement {
 public:
  static constexpr const char* ussClassName = "unity-repeat-button";
  RepeatButton();
  RepeatButton(std::function<void()> action, double delay_ms, double interval_ms);
  const char* typeName() const override { return "RepeatButton"; }
  void SetAction(std::function<void()> action, double delay_ms, double interval_ms);

 private:
  Clickable* clickable_ = nullptr;
};

// --------------------------------------------------------------- Image ----

class Image : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-image";
  Image();
  const char* typeName() const override { return "Image"; }

  // A file (res://, mercury:// or absolute) or a texture made in code.
  void SetImage(std::string path);
  void SetTexture(uint32_t texture_id);
  const std::string& imagePath() const { return path_; }
  ScaleMode scaleMode() const { return scale_mode_; }
  void SetScaleMode(ScaleMode mode);
  Color tintColor() const { return tint_; }
  void SetTintColor(Color tint);
  // Sub-rectangle of the texture, 0..1 (x, y, width, height).
  glm::vec4 uv() const { return uv_; }
  void SetUV(glm::vec4 uv);

  bool HasMeasure() const override { return true; }
  glm::vec2 Measure(float width, MeasureMode width_mode, float height,
                    MeasureMode height_mode) override;
  void GenerateVisualContent(MeshGenerationContext& context) override;
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 private:
  std::shared_ptr<UITexture> Texture() const;
  std::string path_;
  uint32_t texture_id_ = 0;
  ScaleMode scale_mode_ = ScaleMode::kScaleToFit;
  Color tint_ = Color::White();
  glm::vec4 uv_{0, 0, 1, 1};
};

// ----------------------------------------------------------- Containers ---

class Box : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-box";
  Box() { AddToClassList(ussClassName); }
  const char* typeName() const override { return "Box"; }
};

class GroupBox : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-group-box";
  GroupBox();
  explicit GroupBox(std::string text) : GroupBox() { SetText(std::move(text)); }
  const char* typeName() const override { return "GroupBox"; }
  const std::string& text() const;
  void SetText(std::string text);
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 private:
  Label* label_ = nullptr;
};

// ---------------------------------------------------------- Base fields ---

/**
 * @brief A control with an optional label and a value (Unity's BaseField<T>)
 */
template <typename T>
class BaseField : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-base-field";
  static constexpr const char* labelUssClassName = "unity-base-field__label";
  static constexpr const char* inputUssClassName = "unity-base-field__input";

  const T& value() const { return value_; }
  // Sets the value and sends a ChangeEvent<T> if it changed.
  void SetValue(T value) {
    value = Coerce(std::move(value));
    if (value == value_) return;
    T previous = value_;
    SetValueWithoutNotify(std::move(value));
    ChangeEvent<T> event(previous, value_);
    SendEvent(event);
  }
  virtual void SetValueWithoutNotify(T value) {
    value_ = Coerce(std::move(value));
    OnValueChanged();
    NotifyPropertyChanged();
  }

  const std::string& label() const { return label_text_; }
  void SetLabel(std::string text);
  Label* labelElement() const { return label_; }
  VisualElement* visualInput() const { return input_; }

 protected:
  BaseField();
  virtual T Coerce(T value) const { return value; }
  virtual void OnValueChanged() {}

  T value_{};
  VisualElement* input_ = nullptr;

 private:
  std::string label_text_;
  Label* label_ = nullptr;
};

class Toggle : public BaseField<bool> {
 public:
  static constexpr const char* ussClassName = "unity-toggle";
  Toggle();
  explicit Toggle(std::string label) : Toggle() { SetLabel(std::move(label)); }
  const char* typeName() const override { return "Toggle"; }

  // Text shown after the checkmark.
  const std::string& text() const { return text_; }
  void SetText(std::string text);
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;
  void HandleEventBubbleUp(EventBase& event) override;

 protected:
  void OnValueChanged() override;
  VisualElement* checkmark_ = nullptr;
  std::string text_;
  Label* text_label_ = nullptr;
};

class RadioButton : public Toggle {
 public:
  static constexpr const char* ussClassName = "unity-radio-button";
  RadioButton();
  explicit RadioButton(std::string label) : RadioButton() { SetLabel(std::move(label)); }
  const char* typeName() const override { return "RadioButton"; }
  void HandleEventBubbleUp(EventBase& event) override;

 protected:
  void OnValueChanged() override;
};

enum class SliderDirection : uint8_t { kHorizontal, kVertical };

template <typename T>
class BaseSlider : public BaseField<T> {
 public:
  static constexpr const char* ussClassName = "unity-base-slider";

  T lowValue() const { return low_; }
  T highValue() const { return high_; }
  void SetLowValue(T low);
  void SetHighValue(T high);
  float pageSize() const { return page_size_; }
  void SetPageSize(float size) { page_size_ = size; }
  SliderDirection direction() const { return direction_; }
  void SetDirection(SliderDirection direction);
  bool inverted() const { return inverted_; }
  void SetInverted(bool inverted);
  // value mapped to 0..1
  float normalizedValue() const;
  VisualElement* dragger() const { return dragger_; }

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;
  void HandleEventBubbleUp(EventBase& event) override;

 protected:
  BaseSlider(T low, T high);
  T Coerce(T value) const override;
  void OnValueChanged() override;
  void SetFromPointer(glm::vec2 world);
  virtual T FromNormalized(float t) const = 0;

  T low_, high_;
  float page_size_ = 0.0f;
  SliderDirection direction_ = SliderDirection::kHorizontal;
  bool inverted_ = false;
  bool dragging_ = false;
  VisualElement* drag_container_ = nullptr;
  VisualElement* tracker_ = nullptr;
  VisualElement* dragger_ = nullptr;
};

class Slider : public BaseSlider<float> {
 public:
  static constexpr const char* ussClassName = "unity-slider";
  Slider(float low = 0.0f, float high = 10.0f);
  Slider(std::string label, float low = 0.0f, float high = 10.0f)
      : Slider(low, high) {
    SetLabel(std::move(label));
  }
  const char* typeName() const override { return "Slider"; }

 protected:
  float FromNormalized(float t) const override;
};

class SliderInt : public BaseSlider<int> {
 public:
  static constexpr const char* ussClassName = "unity-slider-int";
  SliderInt(int low = 0, int high = 10);
  const char* typeName() const override { return "SliderInt"; }

 protected:
  int FromNormalized(float t) const override;
};

class ProgressBar : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-progress-bar";
  ProgressBar();
  const char* typeName() const override { return "ProgressBar"; }

  float lowValue() const { return low_; }
  float highValue() const { return high_; }
  float value() const { return value_; }
  void SetLowValue(float v);
  void SetHighValue(float v);
  void SetValue(float v);
  const std::string& title() const;
  void SetTitle(std::string title);

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 private:
  void UpdateProgress();
  float low_ = 0.0f, high_ = 100.0f, value_ = 0.0f;
  VisualElement* progress_ = nullptr;
  Label* title_ = nullptr;
};

// ---------------------------------------------------------- Text fields ---

class TextInputBase;

/**
 * @brief Editable text (Unity's TextField), with selection, clipboard,
 * password masking and multiline support
 */
class TextField : public BaseField<std::string> {
 public:
  static constexpr const char* ussClassName = "unity-text-field";
  TextField();
  explicit TextField(std::string label) : TextField() { SetLabel(std::move(label)); }
  const char* typeName() const override { return "TextField"; }

  const std::string& text() const { return value_; }
  void SetText(std::string text) { SetValue(std::move(text)); }

  int maxLength() const { return max_length_; }
  void SetMaxLength(int length) { max_length_ = length; }
  bool multiline() const { return multiline_; }
  void SetMultiline(bool multiline);
  bool isPasswordField() const { return password_; }
  void SetIsPasswordField(bool password);
  char32_t maskChar() const { return mask_char_; }
  void SetMaskChar(char32_t c) { mask_char_ = c; }
  bool isReadOnly() const { return read_only_; }
  void SetIsReadOnly(bool read_only) { read_only_ = read_only; }
  // Only send ChangeEvents on Enter or when focus leaves.
  bool isDelayed() const { return delayed_; }
  void SetIsDelayed(bool delayed) { delayed_ = delayed; }
  // Hint shown while empty.
  const std::string& placeholder() const { return placeholder_; }
  void SetPlaceholder(std::string text);

  int cursorIndex() const { return cursor_; }
  int selectIndex() const { return anchor_; }
  void SelectAll();
  void SelectRange(int anchor, int cursor);

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;
  void HandleEventBubbleUp(EventBase& event) override;

  // Used by the inner text element.
  std::string DisplayText() const;
  void DrawCaretAndSelection(MeshGenerationContext& context);

 protected:
  void SetValueWithoutNotify(std::string value) override;
  std::string Coerce(std::string value) const override;
  // Called with the edited text; numeric fields override it.
  virtual void CommitText(const std::string& text);
  virtual bool AcceptsCharacter(char32_t c) const { return true; }

  std::u32string editing_;  // the text being edited, as codepoints

 private:
  void Insert(std::u32string_view text);
  void DeleteSelection();
  int IndexAt(glm::vec2 world) const;
  void EnsureCursorVisible();
  void ChangeText(std::u32string text);

  TextInputBase* text_input_ = nullptr;
  int max_length_ = -1;
  bool multiline_ = false;
  bool password_ = false;
  char32_t mask_char_ = U'*';
  bool read_only_ = false;
  bool delayed_ = false;
  std::string placeholder_;
  int cursor_ = 0;
  int anchor_ = 0;
  bool dragging_ = false;
  float scroll_x_ = 0.0f;
  double blink_start_ = 0.0;
  bool pending_commit_ = false;
};

class IntegerField : public TextField {
 public:
  static constexpr const char* ussClassName = "unity-integer-field";
  IntegerField();
  const char* typeName() const override { return "IntegerField"; }
  int intValue() const { return int_value_; }
  void SetIntValue(int value);
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 protected:
  void CommitText(const std::string& text) override;
  bool AcceptsCharacter(char32_t c) const override;

 private:
  int int_value_ = 0;
};

class FloatField : public TextField {
 public:
  static constexpr const char* ussClassName = "unity-float-field";
  FloatField();
  const char* typeName() const override { return "FloatField"; }
  float floatValue() const { return float_value_; }
  void SetFloatValue(float value);
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 protected:
  void CommitText(const std::string& text) override;
  bool AcceptsCharacter(char32_t c) const override;

 private:
  float float_value_ = 0.0f;
};

// ---------------------------------------------------------- Scrolling -----

class Scroller : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-scroller";
  explicit Scroller(SliderDirection direction = SliderDirection::kVertical);
  const char* typeName() const override { return "Scroller"; }

  float value() const { return value_; }
  void SetValue(float value);
  float highValue() const { return high_; }
  void SetHighValue(float high);
  // Visible fraction of the content, 0..1; sizes the dragger.
  void SetFactor(float factor);
  UIAction<float> valueChanged;
  void HandleEventBubbleUp(EventBase& event) override;

 private:
  void UpdateDragger();
  SliderDirection direction_;
  float value_ = 0.0f, high_ = 0.0f, factor_ = 1.0f;
  bool dragging_ = false;
  float drag_offset_ = 0.0f;
  VisualElement* tracker_ = nullptr;
  VisualElement* dragger_ = nullptr;
};

enum class ScrollViewMode : uint8_t { kVertical, kHorizontal, kVerticalAndHorizontal };
enum class ScrollerVisibility : uint8_t { kAuto, kAlwaysVisible, kHidden };

class ScrollView : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-scroll-view";
  explicit ScrollView(ScrollViewMode mode = ScrollViewMode::kVertical);
  const char* typeName() const override { return "ScrollView"; }
  VisualElement* contentContainer() override { return content_; }
  VisualElement* contentViewport() const { return viewport_; }

  glm::vec2 scrollOffset() const { return offset_; }
  void SetScrollOffset(glm::vec2 offset);
  void ScrollTo(VisualElement* child);
  ScrollViewMode mode() const { return mode_; }
  void SetMode(ScrollViewMode mode);
  void SetVerticalScrollerVisibility(ScrollerVisibility v);
  void SetHorizontalScrollerVisibility(ScrollerVisibility v);
  float mouseWheelScrollSize() const { return wheel_size_; }
  void SetMouseWheelScrollSize(float size) { wheel_size_ = size; }
  Scroller* verticalScroller() const { return vertical_; }
  Scroller* horizontalScroller() const { return horizontal_; }

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;
  void HandleEventBubbleUp(EventBase& event) override;

 private:
  void UpdateScrollers();
  ScrollViewMode mode_;
  ScrollerVisibility vertical_visibility_ = ScrollerVisibility::kAuto;
  ScrollerVisibility horizontal_visibility_ = ScrollerVisibility::kAuto;
  float wheel_size_ = 18.0f;
  glm::vec2 offset_{0.0f};
  VisualElement* viewport_ = nullptr;
  VisualElement* content_ = nullptr;
  Scroller* vertical_ = nullptr;
  Scroller* horizontal_ = nullptr;
};

class Foldout : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-foldout";
  Foldout();
  const char* typeName() const override { return "Foldout"; }
  VisualElement* contentContainer() override { return content_; }

  const std::string& text() const;
  void SetText(std::string text);
  bool value() const;
  void SetValue(bool open);
  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;

 private:
  Toggle* toggle_ = nullptr;
  VisualElement* content_ = nullptr;
};

enum class SelectionType : uint8_t { kNone, kSingle, kMultiple };

/**
 * @brief A list of items made from a template (Unity's ListView)
 *
 *   list->makeItem = [] { return std::make_unique<Label>(); };
 *   list->bindItem = [&](VisualElement* e, int i) {
 *     static_cast<Label*>(e)->SetText(items[i]);
 *   };
 *   list->SetItemCount(items.size());
 */
class ListView : public VisualElement {
 public:
  static constexpr const char* ussClassName = "unity-list-view";
  ListView();
  const char* typeName() const override { return "ListView"; }

  std::function<std::unique_ptr<VisualElement>()> makeItem;
  std::function<void(VisualElement*, int)> bindItem;
  std::function<void(VisualElement*, int)> unbindItem;

  int itemCount() const { return count_; }
  void SetItemCount(int count);
  // Rebuilds every item (call after the data changed).
  void RefreshItems();
  void Rebuild() { RefreshItems(); }

  float fixedItemHeight() const { return item_height_; }
  void SetFixedItemHeight(float height);
  SelectionType selectionType() const { return selection_type_; }
  void SetSelectionType(SelectionType type) { selection_type_ = type; }
  int selectedIndex() const { return selected_.empty() ? -1 : selected_.front(); }
  const std::vector<int>& selectedIndices() const { return selected_; }
  void SetSelection(int index);
  void AddToSelection(int index);
  void ClearSelection();

  UIAction<const std::vector<int>&> selectionChanged;
  // Double-clicked or submitted items.
  UIAction<const std::vector<int>&> itemsChosen;
  ScrollView* scrollView() const { return scroll_; }

  bool SetProperty(std::string_view name, const mtext::Variant& value) override;
  std::optional<mtext::Variant> GetProperty(std::string_view name) const override;
  void HandleEventBubbleUp(EventBase& event) override;

 private:
  void UpdateSelectionClasses();
  int count_ = 0;
  float item_height_ = 22.0f;
  SelectionType selection_type_ = SelectionType::kSingle;
  std::vector<int> selected_;
  ScrollView* scroll_ = nullptr;
  std::vector<VisualElement*> items_;
};

class UxmlFactoryRegistry;
// Adds every built-in control to the UXML factory (called once by the
// registry when it is created).
void RegisterBuiltinControls(UxmlFactoryRegistry& registry);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_CONTROLS_H
