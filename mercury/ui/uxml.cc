#include "uxml.h"

#include <tinyxml2.h>

#include <algorithm>
#include <format>
#include <sstream>

#include "debugger/debugger.h"
#include "ui/controls.h"
#include "ui/resources.h"

namespace Mercury::UI {

namespace {

std::string LocalName(std::string_view tag) {
  const size_t colon = tag.rfind(':');
  return std::string(colon == std::string_view::npos ? tag : tag.substr(colon + 1));
}

// "UnityEngine.UIElements.Button" style fully qualified names.
std::string TypeNameOf(std::string_view tag) {
  std::string name = LocalName(tag);
  const size_t dot = name.rfind('.');
  return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::string Escape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\n': out += "&#10;"; break;
      default: out += c;
    }
  }
  return out;
}

void ParseElement(const tinyxml2::XMLElement* xml, UxmlNode& node,
                  std::vector<UxmlError>* errors) {
  node.type = TypeNameOf(xml->Name());
  node.line = xml->GetLineNum();
  for (const auto* a = xml->FirstAttribute(); a; a = a->Next()) {
    node.attributes.push_back({a->Name(), a->Value()});
  }
  for (const auto* child = xml->FirstChildElement(); child;
       child = child->NextSiblingElement()) {
    const std::string type = TypeNameOf(child->Name());
    if (type == "Style") {
      if (const char* src = child->Attribute("src")) {
        node.style_sheets.emplace_back(src);
      } else if (const char* p = child->Attribute("path")) {
        node.style_sheets.emplace_back(p);
      }
    } else if (type == "Bindings") {
      for (const auto* b = child->FirstChildElement(); b; b = b->NextSiblingElement()) {
        UxmlBindingDesc binding;
        if (const char* p = b->Attribute("property")) binding.property = p;
        if (const char* p = b->Attribute("data-source-path")) binding.data_source_path = p;
        if (const char* m = b->Attribute("binding-mode")) {
          if (auto mode = BindingModeFromName(m)) binding.mode = *mode;
        }
        if (binding.property.empty()) {
          if (errors) errors->push_back({b->GetLineNum(), "DataBinding needs a property"});
          continue;
        }
        node.bindings.push_back(std::move(binding));
      }
    } else if (type == "AttributeOverrides") {
      UxmlAttributeOverride o;
      for (const auto* a = child->FirstAttribute(); a; a = a->Next()) {
        if (std::string_view(a->Name()) == "element-name") {
          o.element_name = a->Value();
        } else {
          o.attributes.push_back({a->Name(), a->Value()});
        }
      }
      node.overrides.push_back(std::move(o));
    } else {
      UxmlNode& c = node.children.emplace_back();
      ParseElement(child, c, errors);
    }
  }
}

void WriteNode(const UxmlNode& node, int depth, std::string& out) {
  const std::string indent(depth * 4, ' ');
  const bool known = node.type == "Instance" ||
                     UxmlFactoryRegistry::Instance().Find(node.type) != nullptr;
  const std::string tag = (known ? "ui:" : "") + node.type;
  out += indent + "<" + tag;
  for (const auto& a : node.attributes) {
    out += std::format(" {}=\"{}\"", a.name, Escape(a.value));
  }
  const bool empty = node.children.empty() && node.style_sheets.empty() &&
                     node.bindings.empty() && node.overrides.empty();
  if (empty) {
    out += " />\n";
    return;
  }
  out += ">\n";
  for (const auto& sheet : node.style_sheets) {
    out += std::format("{}    <Style src=\"{}\" />\n", indent, Escape(sheet));
  }
  for (const auto& o : node.overrides) {
    out += std::format("{}    <ui:AttributeOverrides element-name=\"{}\"", indent,
                       Escape(o.element_name));
    for (const auto& a : o.attributes) {
      out += std::format(" {}=\"{}\"", a.name, Escape(a.value));
    }
    out += " />\n";
  }
  if (!node.bindings.empty()) {
    out += indent + "    <Bindings>\n";
    for (const auto& b : node.bindings) {
      out += std::format("{}        <ui:DataBinding property=\"{}\" data-source-path=\"{}\"",
                         indent, Escape(b.property), Escape(b.data_source_path));
      if (b.mode != BindingMode::kToTarget) {
        out += std::format(" binding-mode=\"{}\"", BindingModeName(b.mode));
      }
      out += " />\n";
    }
    out += indent + "    </Bindings>\n";
  }
  for (const auto& child : node.children) WriteNode(child, depth + 1, out);
  out += indent + "</" + tag + ">\n";
}

mtext::Variant ConvertAttribute(const UxmlAttributeDescription* desc,
                                std::string_view value, std::string_view base) {
  if (!desc) return std::string(value);
  switch (desc->type) {
    case FieldType::kBool: {
      std::string v(value);
      std::transform(v.begin(), v.end(), v.begin(), ::tolower);
      return v == "true" || v == "1";
    }
    case FieldType::kInt: return static_cast<int64_t>(std::strtoll(std::string(value).c_str(), nullptr, 10));
    case FieldType::kFloat: return std::strtod(std::string(value).c_str(), nullptr);
    case FieldType::kColor:
      if (auto c = ParseColor(value)) return mtext::Color{c->rgba};
      return std::string(value);
    case FieldType::kAsset: {
      std::string v(value);
      // project://database/... or url("...") forms.
      if (v.starts_with("url(")) {
        auto tokens = TokenizeStyleValue(v);
        if (!tokens.empty() && !tokens[0].args.empty()) v = tokens[0].args[0].text;
      }
      return mtext::Resource{ResolveRelativePath(base, v)};
    }
    default: return std::string(value);
  }
}

void CloneNode(const UxmlNode& node, VisualElement* parent, UIResources& resources,
               const VisualTreeAsset& asset) {
  auto& registry = UxmlFactoryRegistry::Instance();
  if (node.type == "Instance") {
    const std::string* name = node.FindAttribute("template");
    const UxmlTemplateRef* ref = name ? asset.FindTemplate(*name) : nullptr;
    auto container = std::make_unique<TemplateContainer>();
    container->uxmlSource = &node;
    if (ref) {
      container->templateId = ref->name;
      if (auto tree = resources.LoadUxml(ResolveRelativePath(asset.path, ref->src))) {
        tree->CloneTree(container.get(), resources);
      }
    } else {
      PDebug::warn("UI: {}:{}: no <Template> named \"{}\"", asset.path, node.line,
                   name ? *name : "");
    }
    const UxmlTypeInfo* info = registry.Find("TemplateContainer");
    for (const auto& a : node.attributes) {
      if (a.name == "template") continue;
      ApplyUxmlAttribute(container.get(), info, a.name, a.value, asset.path);
    }
    for (const auto& o : node.overrides) {
      container->Query<VisualElement>(o.element_name).ForEach([&](VisualElement* e) {
        const UxmlTypeInfo* type = registry.Find(e->typeName());
        for (const auto& a : o.attributes) {
          ApplyUxmlAttribute(e, type, a.name, a.value, asset.path);
        }
      });
    }
    for (const auto& sheet : node.style_sheets) {
      container->AddStyleSheet(resources.LoadStyleSheet(ResolveRelativePath(asset.path, sheet)));
    }
    parent->Add(std::move(container));
    return;
  }

  const UxmlTypeInfo* info = registry.Find(node.type);
  std::unique_ptr<VisualElement> element;
  if (info) {
    element = info->create();
  } else {
    PDebug::warn("UI: {}:{}: unknown element type \"{}\"", asset.path, node.line, node.type);
    element = std::make_unique<VisualElement>();
  }
  element->uxmlSource = &node;
  for (const auto& a : node.attributes) {
    ApplyUxmlAttribute(element.get(), info, a.name, a.value, asset.path);
  }
  for (const auto& sheet : node.style_sheets) {
    element->AddStyleSheet(resources.LoadStyleSheet(ResolveRelativePath(asset.path, sheet)));
  }
  for (const auto& b : node.bindings) {
    element->SetBinding(b.property, DataBinding{b.data_source_path, b.mode});
  }
  VisualElement* raw = parent->Add(std::move(element));
  for (const auto& child : node.children) CloneNode(child, raw, resources, asset);
}

}  // namespace

const std::string* UxmlNode::FindAttribute(std::string_view name) const {
  for (const auto& a : attributes) {
    if (a.name == name) return &a.value;
  }
  return nullptr;
}

void UxmlNode::SetAttribute(std::string_view name, std::string value) {
  for (auto& a : attributes) {
    if (a.name == name) {
      a.value = std::move(value);
      return;
    }
  }
  attributes.push_back({std::string(name), std::move(value)});
}

void UxmlNode::RemoveAttribute(std::string_view name) {
  std::erase_if(attributes, [&](const UxmlAttribute& a) { return a.name == name; });
}

std::shared_ptr<VisualTreeAsset> VisualTreeAsset::Parse(std::string_view xml,
                                                        std::string path,
                                                        std::vector<UxmlError>* errors) {
  auto asset = std::make_shared<VisualTreeAsset>();
  asset->path = std::move(path);
  tinyxml2::XMLDocument doc;
  if (doc.Parse(xml.data(), xml.size()) != tinyxml2::XML_SUCCESS) {
    if (errors) {
      errors->push_back({doc.ErrorLineNum(), doc.ErrorStr() ? doc.ErrorStr() : "bad XML"});
    }
    return asset;
  }
  const tinyxml2::XMLElement* root = doc.RootElement();
  if (!root || TypeNameOf(root->Name()) != "UXML") {
    if (errors) errors->push_back({root ? root->GetLineNum() : 1, "the root must be <ui:UXML>"});
    return asset;
  }
  for (const auto* a = root->FirstAttribute(); a; a = a->Next()) {
    asset->root.attributes.push_back({a->Name(), a->Value()});
  }
  asset->root.line = root->GetLineNum();
  for (const auto* child = root->FirstChildElement(); child;
       child = child->NextSiblingElement()) {
    const std::string type = TypeNameOf(child->Name());
    if (type == "Style") {
      if (const char* src = child->Attribute("src")) asset->root.style_sheets.emplace_back(src);
    } else if (type == "Template") {
      UxmlTemplateRef ref;
      if (const char* n = child->Attribute("name")) ref.name = n;
      if (const char* s = child->Attribute("src")) ref.src = s;
      else if (const char* p = child->Attribute("path")) ref.src = p;
      asset->templates.push_back(std::move(ref));
    } else {
      UxmlNode& node = asset->root.children.emplace_back();
      ParseElement(child, node, errors);
    }
  }
  return asset;
}

std::string VisualTreeAsset::Write() const {
  std::string out = "<ui:UXML";
  bool has_ns = false;
  for (const auto& a : root.attributes) {
    if (a.name == "xmlns:ui") has_ns = true;
  }
  if (!has_ns) out += " xmlns:ui=\"Mercury.UI\"";
  for (const auto& a : root.attributes) {
    out += std::format(" {}=\"{}\"", a.name, Escape(a.value));
  }
  out += ">\n";
  for (const auto& sheet : root.style_sheets) {
    out += std::format("    <Style src=\"{}\" />\n", Escape(sheet));
  }
  for (const auto& t : templates) {
    out += std::format("    <ui:Template name=\"{}\" src=\"{}\" />\n", Escape(t.name),
                       Escape(t.src));
  }
  for (const auto& child : root.children) WriteNode(child, 1, out);
  out += "</ui:UXML>\n";
  return out;
}

void VisualTreeAsset::CloneTree(VisualElement* target, UIResources& resources) const {
  for (const auto& sheet : root.style_sheets) {
    target->AddStyleSheet(resources.LoadStyleSheet(ResolveRelativePath(path, sheet)));
  }
  for (const auto& child : root.children) CloneNode(child, target, resources, *this);
}

std::unique_ptr<VisualElement> VisualTreeAsset::Instantiate(UIResources& resources) const {
  auto container = std::make_unique<TemplateContainer>();
  container->templateId = path;
  CloneTree(container.get(), resources);
  return container;
}

const UxmlTemplateRef* VisualTreeAsset::FindTemplate(std::string_view name) const {
  for (const auto& t : templates) {
    if (t.name == name) return &t;
  }
  return nullptr;
}

UxmlFactoryRegistry& UxmlFactoryRegistry::Instance() {
  static UxmlFactoryRegistry registry;
  return registry;
}

UxmlFactoryRegistry::UxmlFactoryRegistry() { RegisterBuiltinControls(*this); }

void UxmlFactoryRegistry::Register(UxmlTypeInfo info) {
  for (auto& existing : types_) {
    if (existing.name == info.name) {
      existing = std::move(info);
      return;
    }
  }
  types_.push_back(std::move(info));
}

const UxmlTypeInfo* UxmlFactoryRegistry::Find(std::string_view name) const {
  for (const auto& t : types_) {
    if (t.name == name) return &t;
  }
  return nullptr;
}

std::vector<const UxmlTypeInfo*> UxmlFactoryRegistry::Types() const {
  std::vector<const UxmlTypeInfo*> out;
  for (const auto& t : types_) out.push_back(&t);
  return out;
}

std::vector<UxmlAttributeDescription> VisualElementAttributes() {
  using F = FieldType;
  return {
      {"name", F::kString, "", {}, "", "Used by #name selectors and Q()"},
      {"class", F::kString, "", {}, "", "Space-separated USS classes"},
      {"style", F::kString, "", {}, "", "Inline USS declarations"},
      {"picking-mode", F::kEnum, "Position", {"Position", "Ignore"}, "",
       "Ignore lets pointer events pass through"},
      {"tooltip", F::kString, ""},
      {"focusable", F::kBool, "false"},
      {"tabindex", F::kInt, "0"},
      {"enabled", F::kBool, "true"},
      {"data-source-path", F::kString, "", {}, "", "Prefix for bindings below"},
  };
}

bool ApplyUxmlAttribute(VisualElement* element, const UxmlTypeInfo* type,
                        std::string_view name, std::string_view value,
                        std::string_view base_path) {
  if (name.starts_with("xmlns") || name == "editor-extension-mode" ||
      name == "view-data-key" || name == "content-container") {
    return true;
  }
  if (name == "class") {
    std::istringstream in{std::string(value)};
    std::string cls;
    while (in >> cls) element->AddToClassList(cls);
    return true;
  }
  if (name == "style") {
    for (const auto& d : ParseInlineStyle(value)) {
      for (const auto& expanded : ExpandDeclaration(d.property, d.tokens)) {
        if (auto v = ParseStyleValue(expanded.id, expanded.tokens, base_path)) {
          element->style().Set(expanded.id, *v);
        }
      }
    }
    return true;
  }
  if (name == "binding-path") {
    // Unity's editor-only binding; here it becomes a runtime binding of the
    // element's value (or text).
    const char* property = element->GetProperty("value") ? "value" : "text";
    element->SetBinding(property, DataBinding{std::string(value)});
    return true;
  }
  const UxmlAttributeDescription* desc = nullptr;
  if (type) {
    for (const auto& a : type->attributes) {
      if (a.name == name) desc = &a;
    }
  }
  const std::string property = desc && !desc->property.empty() ? desc->property
                                                               : std::string(name);
  return element->SetProperty(property, ConvertAttribute(desc, value, base_path));
}

}  // namespace Mercury::UI
