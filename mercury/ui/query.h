/*
 * query.h
 *
 * UQuery, Unity's way of finding elements:
 *
 *   auto* score = root->Q<Label>("score");
 *   root->Query<Button>().Class("menu-button").ForEach(
 *       [](Button* b) { b->SetEnabled(false); });
 *
 * Included by visual_element.h.
 */
#ifndef MERCURY_UI_QUERY_H
#define MERCURY_UI_QUERY_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Mercury::UI {

class VisualElement;

template <typename T>
class UQueryBuilder {
 public:
  explicit UQueryBuilder(VisualElement* root) : root_(root) {}

  UQueryBuilder& Name(std::string_view name) {
    names_.emplace_back(name);
    return *this;
  }
  UQueryBuilder& Class(std::string_view class_name) {
    classes_.emplace_back(class_name);
    return *this;
  }
  UQueryBuilder& Where(std::function<bool(T*)> predicate) {
    predicates_.push_back(std::move(predicate));
    return *this;
  }
  // Excludes the root itself from the results.
  UQueryBuilder& Descendents() {
    include_root_ = false;
    return *this;
  }

  std::vector<T*> ToList() const {
    std::vector<T*> out;
    Visit(root_, out, false);
    return out;
  }
  T* First() const {
    std::vector<T*> out;
    Visit(root_, out, true);
    return out.empty() ? nullptr : out.front();
  }
  T* Last() const {
    auto all = ToList();
    return all.empty() ? nullptr : all.back();
  }
  T* AtIndex(size_t index) const {
    auto all = ToList();
    return index < all.size() ? all[index] : nullptr;
  }
  void ForEach(const std::function<void(T*)>& fn) const {
    for (T* element : ToList()) fn(element);
  }

 private:
  bool MatchesElement(VisualElement* element) const;
  bool Visit(VisualElement* element, std::vector<T*>& out, bool first_only) const;

  VisualElement* root_;
  std::vector<std::string> names_;
  std::vector<std::string> classes_;
  std::vector<std::function<bool(T*)>> predicates_;
  bool include_root_ = true;
};

template <typename T>
bool UQueryBuilder<T>::MatchesElement(VisualElement* element) const {
  T* typed = dynamic_cast<T*>(element);
  if (!typed) return false;
  for (const auto& name : names_) {
    if (element->name() != name) return false;
  }
  for (const auto& cls : classes_) {
    if (!element->ClassListContains(cls)) return false;
  }
  for (const auto& predicate : predicates_) {
    if (!predicate(typed)) return false;
  }
  return true;
}

template <typename T>
bool UQueryBuilder<T>::Visit(VisualElement* element, std::vector<T*>& out,
                             bool first_only) const {
  if (!element) return false;
  if ((element != root_ || include_root_) && MatchesElement(element)) {
    out.push_back(static_cast<T*>(dynamic_cast<T*>(element)));
    if (first_only) return true;
  }
  for (const auto& child : element->hierarchyChildren()) {
    if (Visit(child.get(), out, first_only)) return true;
  }
  return false;
}

template <typename T>
T* VisualElement::Q(std::string_view name, std::string_view class_name) {
  UQueryBuilder<T> query(this);
  if (!name.empty()) query.Name(name);
  if (!class_name.empty()) query.Class(class_name);
  return query.First();
}

template <typename T>
UQueryBuilder<T> VisualElement::Query(std::string_view name,
                                      std::string_view class_name) {
  UQueryBuilder<T> query(this);
  if (!name.empty()) query.Name(name);
  if (!class_name.empty()) query.Class(class_name);
  return query;
}

}  // namespace Mercury::UI

#endif  // MERCURY_UI_QUERY_H
