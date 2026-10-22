#include "map.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace Mercury {

namespace {

uint64_t NextUid() {
  static std::atomic<uint64_t> next{1};
  return next++;
}

// Applies fn to every value in a property list, descending into arrays.
void ForEachValue(mtext::PropertyList& properties,
                  const std::function<void(mtext::Variant&)>& fn) {
  std::function<void(mtext::Variant&)> visit = [&](mtext::Variant& value) {
    if (auto array = value.Get<mtext::Array>()) {
      for (auto& item : *array) visit(item);
    } else {
      fn(value);
    }
  };
  for (auto& property : properties) visit(property.value);
}

void ForEachValue(const mtext::PropertyList& properties,
                  const std::function<void(const mtext::Variant&)>& fn) {
  std::function<void(const mtext::Variant&)> visit =
      [&](const mtext::Variant& value) {
        if (auto array = value.Get<mtext::Array>()) {
          for (const auto& item : *array) visit(item);
        } else {
          fn(value);
        }
      };
  for (const auto& property : properties) visit(property.value);
}

// Peeks at a .masset/.mmap header to find its type for [ext_resource].
std::string GuessResourceType(const std::string& path) {
  if (path.ends_with(".mmap")) return "Map";
  if (path.ends_with(".uxml")) return "UXML";
  if (path.ends_with(".uss")) return "StyleSheet";
  if (path.ends_with(".ttf") || path.ends_with(".otf")) return "Font";
  if (path.ends_with(".png") || path.ends_with(".jpg") ||
      path.ends_with(".jpeg") || path.ends_with(".bmp") ||
      path.ends_with(".tga")) {
    return "Texture";
  }
  return "Resource";
}

}  // namespace

std::string SanitizeNodeName(std::string_view name) {
  std::string out;
  for (char c : name) {
    if (c == '/' || c == '"' || c == '\\' || c == '\n' || c == '\r') continue;
    out.push_back(c);
  }
  // "." and ".." would read as path components.
  if (out.empty() || out == "." || out == "..") out = "Node";
  return out;
}

// ---------------------------------------------------------------- MapNode ---

ComponentData* MapNode::FindComponent(std::string_view type) {
  for (auto& component : components) {
    if (component.type == type) return &component;
  }
  return nullptr;
}

const ComponentData* MapNode::FindComponent(std::string_view type) const {
  for (const auto& component : components) {
    if (component.type == type) return &component;
  }
  return nullptr;
}

bool MapNode::IsAncestorOf(const MapNode* node) const {
  for (auto p = node ? node->parent : nullptr; p; p = p->parent) {
    if (p == this) return true;
  }
  return false;
}

int MapNode::IndexInParent() const {
  if (!parent) return 0;
  for (size_t i = 0; i < parent->children.size(); i++) {
    if (parent->children[i].get() == this) return static_cast<int>(i);
  }
  return -1;
}

std::string MapNode::GetPath() const {
  if (!parent) return ".";
  if (!parent->parent) return name;
  return parent->GetPath() + "/" + name;
}

// -------------------------------------------------------------------- Map ---

Map::Map() {
  root_ = std::make_unique<MapNode>();
  root_->name = "Root";
  root_->uid = NextUid();
}

Map::Map(const Map& other) { *this = other; }

Map& Map::operator=(const Map& other) {
  if (this == &other) return *this;
  root_ = CloneNode(*other.root_, nullptr);
  resource_types_ = other.resource_types_;
  return *this;
}

std::unique_ptr<MapNode> Map::CloneNode(const MapNode& node, MapNode* parent) {
  auto copy = std::make_unique<MapNode>();
  copy->name = node.name;
  copy->parent = parent;
  copy->components = node.components;
  // Copies keep their uids so editor state (selection, entity links) carries
  // over between snapshots of the same map.
  copy->uid = node.uid;
  for (const auto& child : node.children) {
    copy->children.push_back(CloneNode(*child, copy.get()));
  }
  return copy;
}

void Map::AssignUids(MapNode& node) {
  node.uid = NextUid();
  for (auto& child : node.children) AssignUids(*child);
}

bool Map::LoadFromString(std::string_view source, std::string* error) {
  auto fail = [&](int line, std::string message) {
    if (error) *error = std::format("{}: {}", line, message);
    return false;
  };

  mtext::Document document;
  mtext::ParseError parse_error;
  if (!mtext::Parse(source, document, &parse_error)) {
    if (error) *error = parse_error.ToString();
    return false;
  }
  if (document.sections.empty() || document.sections[0].tag != "mmap") {
    return fail(1, "a map must start with an [mmap format=1] header");
  }
  const auto format = document.sections[0].attributes.GetInt("format", 1);
  if (format > kMapFormatVersion) {
    return fail(1, std::format("map format {} is newer than this engine ({})",
                               format, kMapFormatVersion));
  }

  std::map<std::string, std::string> ext_paths;  // id -> res path
  std::map<std::string, std::string> resource_types;
  std::unique_ptr<MapNode> root;

  for (size_t i = 1; i < document.sections.size(); i++) {
    const auto& section = document.sections[i];
    if (section.tag == "ext_resource") {
      auto id = section.attributes.GetString("id");
      auto path = section.attributes.GetString("path");
      if (id.empty()) return fail(section.line, "ext_resource needs an id");
      ext_paths[id] = path;
      resource_types[path] = section.attributes.GetString("type", "Resource");
    } else if (section.tag == "node") {
      auto name = section.attributes.GetString("name");
      if (name.empty()) return fail(section.line, "node needs a name");
      if (!section.attributes.Has("parent")) {
        if (root) return fail(section.line, "only one node may omit parent");
        root = std::make_unique<MapNode>();
        root->name = name;
        continue;
      }
      if (!root) return fail(section.line, "the root node must come first");
      auto parent_path = section.attributes.GetString("parent");
      MapNode* parent = const_cast<MapNode*>(FindFrom(root.get(), parent_path));
      if (!parent) {
        return fail(section.line,
                    std::format("parent \"{}\" of node \"{}\" not found "
                                "(parents must come before children)",
                                parent_path, name));
      }
      for (const auto& sibling : parent->children) {
        if (sibling->name == name) {
          return fail(section.line,
                      std::format("duplicate node name \"{}\"", name));
        }
      }
      auto node = std::make_unique<MapNode>();
      node->name = name;
      node->parent = parent;
      parent->children.push_back(std::move(node));
    } else if (section.tag == "component") {
      if (!root) return fail(section.line, "component before any node");
      auto type = section.attributes.GetString("type");
      if (type.empty()) return fail(section.line, "component needs a type");
      auto node_path = section.attributes.GetString("node", ".");
      MapNode* node = const_cast<MapNode*>(FindFrom(root.get(), node_path));
      if (!node) {
        return fail(section.line,
                    std::format("component {} refers to missing node \"{}\"",
                                type, node_path));
      }
      ComponentData component{type, section.properties};
      std::string missing;
      ForEachValue(component.properties, [&](mtext::Variant& value) {
        if (auto ext = value.Get<mtext::ExtResource>()) {
          auto it = ext_paths.find(ext->id);
          if (it == ext_paths.end()) {
            missing = ext->id;
            return;
          }
          value = mtext::Variant(mtext::Resource{it->second});
        }
      });
      if (!missing.empty()) {
        return fail(section.line,
                    std::format("ExtResource(\"{}\") is not declared", missing));
      }
      node->components.push_back(std::move(component));
    }
    // Unknown sections are ignored so newer files still open.
  }

  if (!root) return fail(1, "map has no root node");
  root_ = std::move(root);
  AssignUids(*root_);
  resource_types_ = std::move(resource_types);
  return true;
}

bool Map::Load(const std::filesystem::path& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) *error = "could not open " + path.string();
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string inner_error;
  if (!LoadFromString(buffer.str(), &inner_error)) {
    if (error) *error = path.string() + ":" + inner_error;
    return false;
  }
  return true;
}

std::string Map::SaveToString(const ResourceTypeFn& resource_type) const {
  mtext::Document document;
  document.Add("mmap").attributes.Set("format", kMapFormatVersion);

  // Number the referenced resources in the order they first appear.
  std::vector<std::string> resource_order;
  std::map<std::string, std::string> resource_ids;
  Visit([&](const MapNode& node) {
    for (const auto& component : node.components) {
      ForEachValue(component.properties, [&](const mtext::Variant& value) {
        auto resource = value.Get<mtext::Resource>();
        if (!resource || resource->path.empty()) return;
        if (resource_ids.contains(resource->path)) return;
        resource_ids[resource->path] = std::to_string(resource_order.size() + 1);
        resource_order.push_back(resource->path);
      });
    }
  });
  for (const auto& path : resource_order) {
    std::string type;
    if (resource_type) type = resource_type(path);
    if (type.empty()) {
      auto it = resource_types_.find(path);
      type = it != resource_types_.end() ? it->second : GuessResourceType(path);
    }
    auto& section = document.Add("ext_resource");
    section.attributes.Set("type", type);
    section.attributes.Set("path", path);
    section.attributes.Set("id", resource_ids[path]);
  }

  Visit([&](const MapNode& node) {
    auto& node_section = document.Add("node");
    node_section.attributes.Set("name", node.name);
    if (node.parent) node_section.attributes.Set("parent", node.parent->GetPath());
    const std::string path = node.GetPath();
    for (const auto& component : node.components) {
      auto& section = document.Add("component");
      section.attributes.Set("type", component.type);
      section.attributes.Set("node", path);
      section.properties = component.properties;
      ForEachValue(section.properties, [&](mtext::Variant& value) {
        auto resource = value.Get<mtext::Resource>();
        if (!resource || resource->path.empty()) return;
        value = mtext::Variant(mtext::ExtResource{resource_ids[resource->path]});
      });
    }
  });
  return mtext::Write(document);
}

bool Map::Save(const std::filesystem::path& path,
               const ResourceTypeFn& resource_type) const {
  mtext::Document document;
  // Round-trip through the parser so SaveFile's atomic write is reused.
  if (!mtext::Parse(SaveToString(resource_type), document)) return false;
  return mtext::SaveFile(path, document);
}

MapNode* Map::FindNode(std::string_view path) {
  return const_cast<MapNode*>(std::as_const(*this).FindNode(path));
}

const MapNode* Map::FindNode(std::string_view path) const {
  return FindFrom(root_.get(), path);
}

const MapNode* Map::FindFrom(const MapNode* root, std::string_view path) {
  const MapNode* node = root;
  if (!node) return nullptr;
  if (path.empty() || path == ".") return node;
  while (!path.empty()) {
    auto slash = path.find('/');
    auto part = path.substr(0, slash);
    path = slash == std::string_view::npos ? std::string_view()
                                           : path.substr(slash + 1);
    if (part.empty() || part == ".") continue;
    const MapNode* next = nullptr;
    for (const auto& child : node->children) {
      if (child->name == part) {
        next = child.get();
        break;
      }
    }
    if (!next) return nullptr;
    node = next;
  }
  return node;
}

MapNode* Map::FindByUid(uint64_t uid) {
  MapNode* found = nullptr;
  Visit([&](MapNode& node) {
    if (node.uid == uid) found = &node;
  });
  return found;
}

std::string Map::UniqueChildName(const MapNode* parent, std::string_view base,
                                 const MapNode* ignore) const {
  std::string clean = SanitizeNodeName(base);
  if (!parent) return clean;
  auto taken = [&](const std::string& name) {
    for (const auto& child : parent->children) {
      if (child.get() != ignore && child->name == name) return true;
    }
    return false;
  };
  if (!taken(clean)) return clean;
  // Strip an existing numeric suffix so "Wall2" duplicates to "Wall3".
  std::string stem = clean;
  while (!stem.empty() && std::isdigit(static_cast<unsigned char>(stem.back()))) {
    stem.pop_back();
  }
  if (stem.empty()) stem = clean;
  for (int i = 2;; i++) {
    std::string candidate = stem + std::to_string(i);
    if (!taken(candidate)) return candidate;
  }
}

MapNode* Map::AddNode(MapNode* parent, std::string_view name, int index) {
  if (!parent) parent = root_.get();
  auto node = std::make_unique<MapNode>();
  node->name = UniqueChildName(parent, name);
  node->parent = parent;
  node->uid = NextUid();
  MapNode* raw = node.get();
  if (index < 0 || index >= static_cast<int>(parent->children.size())) {
    parent->children.push_back(std::move(node));
  } else {
    parent->children.insert(parent->children.begin() + index, std::move(node));
  }
  return raw;
}

bool Map::RemoveNode(MapNode* node) {
  if (!node || !node->parent) return false;
  auto& siblings = node->parent->children;
  auto it = std::find_if(siblings.begin(), siblings.end(),
                         [&](const auto& child) { return child.get() == node; });
  if (it == siblings.end()) return false;
  siblings.erase(it);
  return true;
}

MapNode* Map::DuplicateNode(MapNode* node) {
  if (!node || !node->parent) return nullptr;
  auto copy = CloneNode(*node, node->parent);
  AssignUids(*copy);
  copy->name = UniqueChildName(node->parent, node->name);
  MapNode* raw = copy.get();
  auto& siblings = node->parent->children;
  siblings.insert(siblings.begin() + node->IndexInParent() + 1, std::move(copy));
  return raw;
}

bool Map::ReparentNode(MapNode* node, MapNode* new_parent, int index) {
  if (!node || !node->parent || !new_parent) return false;
  if (node == new_parent || node->IsAncestorOf(new_parent)) return false;

  const std::string old_path = node->GetPath();
  auto& old_siblings = node->parent->children;
  auto it = std::find_if(old_siblings.begin(), old_siblings.end(),
                         [&](const auto& child) { return child.get() == node; });
  if (it == old_siblings.end()) return false;
  const bool same_parent = node->parent == new_parent;
  const int old_index = static_cast<int>(it - old_siblings.begin());
  std::unique_ptr<MapNode> owned = std::move(*it);
  old_siblings.erase(it);

  // Moving down within the same parent: the removal shifted the target.
  if (same_parent && index > old_index) index--;

  owned->parent = new_parent;
  owned->name = UniqueChildName(new_parent, owned->name);
  auto& new_siblings = new_parent->children;
  if (index < 0 || index >= static_cast<int>(new_siblings.size())) {
    new_siblings.push_back(std::move(owned));
  } else {
    new_siblings.insert(new_siblings.begin() + index, std::move(owned));
  }
  RewriteNodePaths(old_path, node->GetPath());
  return true;
}

std::string Map::RenameNode(MapNode* node, std::string_view new_name) {
  if (!node) return "";
  const std::string old_path = node->GetPath();
  node->name = UniqueChildName(node->parent, new_name, node);
  if (node->parent) RewriteNodePaths(old_path, node->GetPath());
  return node->name;
}

void Map::RewriteNodePaths(const std::string& old_prefix,
                           const std::string& new_prefix) {
  if (old_prefix == new_prefix || old_prefix == ".") return;
  Visit([&](MapNode& node) {
    for (auto& component : node.components) {
      ForEachValue(component.properties, [&](mtext::Variant& value) {
        auto path = value.Get<mtext::NodePath>();
        if (!path) return;
        if (path->path == old_prefix) {
          path->path = new_prefix;
        } else if (path->path.starts_with(old_prefix + "/")) {
          path->path = new_prefix + path->path.substr(old_prefix.size());
        }
      });
    }
  });
}

void Map::Visit(const std::function<void(MapNode&)>& fn) {
  std::function<void(MapNode&)> walk = [&](MapNode& node) {
    fn(node);
    for (auto& child : node.children) walk(*child);
  };
  if (root_) walk(*root_);
}

void Map::Visit(const std::function<void(const MapNode&)>& fn) const {
  std::function<void(const MapNode&)> walk = [&](const MapNode& node) {
    fn(node);
    for (const auto& child : node.children) walk(*child);
  };
  if (root_) walk(*root_);
}

size_t Map::NodeCount() const {
  size_t count = 0;
  Visit([&](const MapNode&) { count++; });
  return count;
}

}  // namespace Mercury
