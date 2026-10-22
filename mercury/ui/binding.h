/*
 * binding.h
 *
 * Runtime data binding, after Unity 6. A binding connects a property of an
 * element ("text", "value", ...) to a path in a data source:
 *
 *   label->SetBinding("text", DataBinding{"score"});
 *   slider->SetBinding("value", DataBinding{"volume", BindingMode::kTwoWay});
 *
 * or in UXML:
 *
 *   <ui:Label text="0">
 *     <Bindings>
 *       <ui:DataBinding property="text" data-source-path="score" />
 *     </Bindings>
 *   </ui:Label>
 *
 * The panel updates bindings once per frame and only touches values that
 * changed.
 */
#ifndef MERCURY_UI_BINDING_H
#define MERCURY_UI_BINDING_H

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "serialization/mtext.h"
#include "ui/data_source.h"
#include "ui/style.h"

namespace Mercury::UI {

enum class BindingMode : uint8_t {
  kTwoWay,
  kToSource,      // UI -> data
  kToTarget,      // data -> UI (the default)
  kToTargetOnce,  // data -> UI, a single time
};

const char* BindingModeName(BindingMode mode);
std::optional<BindingMode> BindingModeFromName(std::string_view name);

struct DataBinding {
  std::string dataSourcePath;
  BindingMode bindingMode = BindingMode::kToTarget;
  // Overrides the element's (inherited) data source.
  std::shared_ptr<IDataSource> dataSource;
  // Optional conversions between the source value and the UI value.
  std::function<mtext::Variant(const mtext::Variant&)> sourceToUi;
  std::function<mtext::Variant(const mtext::Variant&)> uiToSource;

  DataBinding() = default;
  DataBinding(std::string path, BindingMode mode = BindingMode::kToTarget)
      : dataSourcePath(std::move(path)), bindingMode(mode) {}

  // Last values seen, so only changes are pushed.
  std::optional<mtext::Variant> last_source;
  std::optional<mtext::Variant> last_ui;
  bool updated_once = false;
};

// Lenient conversions used by element properties.
std::string VariantToString(const mtext::Variant& value);
std::optional<double> VariantToFloat(const mtext::Variant& value);
std::optional<int64_t> VariantToInt(const mtext::Variant& value);
std::optional<bool> VariantToBool(const mtext::Variant& value);
std::optional<Color> VariantToColor(const mtext::Variant& value);

}  // namespace Mercury::UI

#endif  // MERCURY_UI_BINDING_H
