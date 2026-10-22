/*
 * data_source.h
 *
 * Where runtime data bindings read and write values (Unity 6's dataSource).
 *
 *   // Any Behaviour: fields from Reflect() and ReflectData() are bindable.
 *   root->SetDataSource(AsDataSource());
 *
 *   // Or a simple property bag:
 *   auto data = std::make_shared<UIDataSource>();
 *   data->Set("score", 10);
 *   root->SetDataSource(data);
 */
#ifndef MERCURY_UI_DATA_SOURCE_H
#define MERCURY_UI_DATA_SOURCE_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "serialization/mtext.h"

namespace Mercury {
class Reflector;
}

namespace Mercury::UI {

class IDataSource {
 public:
  virtual ~IDataSource() = default;
  // `path` is the binding's full path, e.g. "score" or "player.health".
  virtual std::optional<mtext::Variant> GetValue(std::string_view path) const = 0;
  virtual bool SetValue(std::string_view path, const mtext::Variant& value) = 0;
};

/**
 * @brief A plain map of path to value
 */
class UIDataSource : public IDataSource {
 public:
  void Set(std::string_view path, mtext::Variant value) {
    values_[std::string(path)] = std::move(value);
  }
  std::optional<mtext::Variant> GetValue(std::string_view path) const override;
  bool SetValue(std::string_view path, const mtext::Variant& value) override;

 private:
  std::map<std::string, mtext::Variant, std::less<>> values_;
};

/**
 * @brief Reads and writes the fields an object lists through a Reflector
 *
 * The object must outlive the source, or pass a `lifetime` token that
 * expires with it (Behaviour::AsDataSource does this).
 */
class ReflectedDataSource : public IDataSource {
 public:
  using ReflectFn = std::function<void(Reflector&)>;
  explicit ReflectedDataSource(ReflectFn reflect,
                               std::weak_ptr<void> lifetime = {});

  std::optional<mtext::Variant> GetValue(std::string_view path) const override;
  bool SetValue(std::string_view path, const mtext::Variant& value) override;

  // Wraps anything with a Reflect(Reflector&) method.
  template <typename T>
  static std::shared_ptr<ReflectedDataSource> Of(T* object) {
    return std::make_shared<ReflectedDataSource>(
        [object](Reflector& r) { object->Reflect(r); });
  }

 private:
  bool Alive() const;
  ReflectFn reflect_;
  std::weak_ptr<void> lifetime_;
  bool has_lifetime_ = false;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_DATA_SOURCE_H
