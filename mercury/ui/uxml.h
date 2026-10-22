/*
 * uxml.h
 *
 * UXML documents, as in Unity UI Toolkit:
 *
 *   <ui:UXML xmlns:ui="Mercury.UI">
 *     <Style src="hud.uss" />
 *     <ui:Template name="Row" src="row.uxml" />
 *     <ui:VisualElement name="top-bar" class="bar" style="height: 40px;">
 *       <ui:Label name="score" text="0" />
 *       <ui:Button name="pause" text="Pause" />
 *       <ui:Instance template="Row">
 *         <AttributeOverrides element-name="title" text="Row 1" />
 *       </ui:Instance>
 *     </ui:VisualElement>
 *   </ui:UXML>
 *
 * Tag prefixes (ui:, engine:) are ignored. A VisualTreeAsset is the parsed
 * description; CloneTree() builds real elements from it. The UI Builder
 * edits the description and writes it back with Write().
 */
#ifndef MERCURY_UI_UXML_H
#define MERCURY_UI_UXML_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/reflection.h"
#include "ui/binding.h"
#include "ui/visual_element.h"

namespace Mercury::UI {

class UIResources;

struct UxmlAttribute {
  std::string name;
  std::string value;
};

struct UxmlBindingDesc {
  std::string property;
  std::string data_source_path;
  BindingMode mode = BindingMode::kToTarget;
};

struct UxmlAttributeOverride {
  std::string element_name;
  std::vector<UxmlAttribute> attributes;
};

struct UxmlNode {
  std::string type;  // "VisualElement", "Button", or "Instance"
  std::vector<UxmlAttribute> attributes;
  std::vector<std::string> style_sheets;  // <Style src> children
  std::vector<UxmlBindingDesc> bindings;
  std::vector<UxmlAttributeOverride> overrides;  // for Instance
  std::vector<UxmlNode> children;
  int line = 0;

  const std::string* FindAttribute(std::string_view name) const;
  void SetAttribute(std::string_view name, std::string value);
  void RemoveAttribute(std::string_view name);
  std::string Name() const {
    auto n = FindAttribute("name");
    return n ? *n : "";
  }
};

struct UxmlTemplateRef {
  std::string name;
  std::string src;
};

struct UxmlError {
  int line = 0;
  std::string message;
};

class VisualTreeAsset {
 public:
  std::string path;
  std::vector<UxmlTemplateRef> templates;
  // Type "UXML"; its children are the document's top-level elements and its
  // style_sheets apply to the whole document.
  UxmlNode root{"UXML"};

  static std::shared_ptr<VisualTreeAsset> Parse(std::string_view xml,
                                                std::string path = "",
                                                std::vector<UxmlError>* errors = nullptr);
  std::string Write() const;

  /**
   * @brief Builds the document's elements and adds them to `target`
   *
   * Style sheets of the document are added to `target`. Elements get
   * uxmlSource set to their node.
   */
  void CloneTree(VisualElement* target, UIResources& resources) const;
  // Like CloneTree into a new TemplateContainer.
  std::unique_ptr<VisualElement> Instantiate(UIResources& resources) const;

  const UxmlTemplateRef* FindTemplate(std::string_view name) const;
};

/**
 * @brief How to make an element type from UXML, and which attributes it has
 */
struct UxmlAttributeDescription {
  std::string name;  // e.g. "text", "low-value"
  FieldType type = FieldType::kString;
  std::string default_value;
  std::vector<std::string> enum_values;
  std::string property;  // SetProperty name; defaults to `name`
  std::string tooltip;
};

struct UxmlTypeInfo {
  std::string name;      // "Button"
  std::string category;  // "Containers" or "Controls", for the UI Builder
  std::function<std::unique_ptr<VisualElement>()> create;
  std::vector<UxmlAttributeDescription> attributes;  // includes base types'
};

class UxmlFactoryRegistry {
 public:
  static UxmlFactoryRegistry& Instance();
  void Register(UxmlTypeInfo info);
  const UxmlTypeInfo* Find(std::string_view name) const;
  std::vector<const UxmlTypeInfo*> Types() const;

 private:
  UxmlFactoryRegistry();
  std::vector<UxmlTypeInfo> types_;
};

// Attributes every element has (name, class, style, picking-mode, ...).
std::vector<UxmlAttributeDescription> VisualElementAttributes();

// Applies one UXML attribute to an element (the same path CloneTree uses).
bool ApplyUxmlAttribute(VisualElement* element, const UxmlTypeInfo* type,
                        std::string_view name, std::string_view value,
                        std::string_view base_path);

/**
 * @brief The element a template instance makes; holds the template's
 * children
 */
class TemplateContainer : public VisualElement {
 public:
  const char* typeName() const override { return "TemplateContainer"; }
  std::string templateId;
};

}  // namespace Mercury::UI

#endif  // MERCURY_UI_UXML_H
