#include "map.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <format>
#include <utility>

#include "os/filesystem.h"
#include "project/asset_database.h"

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

// The [ext_resource] type of a file, from its extension.
std::string GuessResourceType(const std::string& path) {
  std::string type = AssetDatabase::TypeFromExtension(path);
  return type.empty() ? "Resource" : type;
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

ComponentData* MapNode::FindComponent(std::string_view type, int index) {
  return const_cast<ComponentData*>(
      std::as_const(*this).FindComponent(type, index));
}

const ComponentData* MapNode::FindComponent(std::string_view type,
                                            int index) const {
  for (const auto& component : components) {
    if (component.type == type && index-- == 0) return &component;
  }
  return nullptr;
}

const MapNode* MapNode::GetPrefabInstanceRoot() const {
  for (const MapNode* node = this; node; node = node->parent) {
    if (!node->prefab.empty()) return node;
    if (!node->from_prefab) return nullptr;
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
  copy->tag = node.tag;
  copy->layer = node.layer;
  copy->active = node.active;
  copy->prefab = node.prefab;
  copy->from_prefab = node.from_prefab;
  copy->prefab_base = node.prefab_base;
  copy->prefab_base_tag = node.prefab_base_tag;
  copy->prefab_base_layer = node.prefab_base_layer;
  copy->prefab_base_active = node.prefab_base_active;
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

namespace {

// Index of `component` among the node's components of the same type.
int IndexAmongType(const MapNode& node, const ComponentData* component) {
  int index = 0;
  for (const auto& c : node.components) {
    if (&c == component) return index;
    if (c.type == component->type) index++;
  }
  return index;
}

void ReadNodeAttributes(const mtext::PropertyList& attributes, MapNode& node) {
  if (attributes.Has("tag")) node.tag = attributes.GetString("tag");
  if (attributes.Has("layer")) node.layer = static_cast<int>(attributes.GetInt("layer"));
  if (attributes.Has("active")) node.active = attributes.GetBool("active", true);
}

// Marks a node (and its subtree) as created by a prefab.
void MarkFromPrefab(MapNode& node) {
  node.from_prefab = true;
  for (auto& child : node.children) MarkFromPrefab(*child);
}

// Remembers the prefab's values for an instance root and the nodes the
// prefab created, so saving can write only what differs.
void RememberPrefabBase(MapNode& node) {
  node.prefab_base = node.components;
  node.prefab_base_tag = node.tag;
  node.prefab_base_layer = node.layer;
  node.prefab_base_active = node.active;
  for (auto& child : node.children) {
    if (child->from_prefab) RememberPrefabBase(*child);
  }
}

bool HasPrefabBase(const MapNode& node) {
  return node.from_prefab || !node.prefab.empty();
}

}  // namespace

bool Map::LoadFromString(std::string_view source, std::string* error,
                         const PrefabLoaderFn& prefabs) {
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
  if (document.sections.empty() || (document.sections[0].tag != "mmap" &&
                                    document.sections[0].tag != "mprefab")) {
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

  auto resolve_ext = [&](mtext::PropertyList& properties) -> std::string {
    std::string missing;
    ForEachValue(properties, [&](mtext::Variant& value) {
      if (auto ext = value.Get<mtext::ExtResource>()) {
        auto it = ext_paths.find(ext->id);
        if (it == ext_paths.end()) {
          missing = ext->id;
          return;
        }
        value = mtext::Variant(mtext::Resource{it->second});
      }
    });
    return missing;
  };

  // Fills `node` (already named and parented) from the prefab at `path`.
  auto expand_prefab = [&](MapNode& node, const std::string& path,
                           int line) -> bool {
    node.prefab = path;
    const Map* prefab = prefabs ? prefabs(path) : nullptr;
    if (!prefab) {
      // Without the prefab the node stays empty but keeps its link, so the
      // file still saves back unchanged.
      if (prefabs) {
        if (error) *error = std::format("{}: could not load prefab {}", line, path);
      }
      return !prefabs;
    }
    const MapNode& source_root = *prefab->GetRoot();
    node.components = source_root.components;
    node.tag = source_root.tag;
    node.layer = source_root.layer;
    node.active = source_root.active;
    for (const auto& child : source_root.children) {
      node.children.push_back(CloneNode(*child, &node));
    }
    for (auto& child : node.children) MarkFromPrefab(*child);
    RebaseNodePaths(node, node.GetPath());
    RememberPrefabBase(node);
    return true;
  };

  for (size_t i = 1; i < document.sections.size(); i++) {
    auto& section = document.sections[i];
    if (section.tag == "ext_resource") {
      auto id = section.attributes.GetString("id");
      auto path = section.attributes.GetString("path");
      if (id.empty()) return fail(section.line, "ext_resource needs an id");
      ext_paths[id] = path;
      resource_types[path] = section.attributes.GetString("type", "Resource");
    } else if (section.tag == "node") {
      auto name = section.attributes.GetString("name");
      if (name.empty()) return fail(section.line, "node needs a name");
      std::string instance;
      if (auto value = section.attributes.Get("instance")) {
        if (auto ext = value->Get<mtext::ExtResource>()) {
          auto it = ext_paths.find(ext->id);
          if (it == ext_paths.end()) {
            return fail(section.line, std::format("ExtResource(\"{}\") is not declared", ext->id));
          }
          instance = it->second;
        } else if (auto res = value->Get<mtext::Resource>()) {
          instance = res->path;
        } else if (auto text = value->Get<std::string>()) {
          instance = *text;
        }
      }
      if (!section.attributes.Has("parent")) {
        if (root) return fail(section.line, "only one node may omit parent");
        root = std::make_unique<MapNode>();
        root->name = name;
        if (!instance.empty() && !expand_prefab(*root, instance, section.line)) {
          return false;
        }
        ReadNodeAttributes(section.attributes, *root);
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
      MapNode* existing = nullptr;
      for (const auto& sibling : parent->children) {
        if (sibling->name == name) existing = sibling.get();
      }
      if (existing) {
        // A node that came from a prefab can have its attributes overridden.
        if (existing->from_prefab && instance.empty()) {
          ReadNodeAttributes(section.attributes, *existing);
          continue;
        }
        return fail(section.line,
                    std::format("duplicate node name \"{}\"", name));
      }
      auto node = std::make_unique<MapNode>();
      node->name = name;
      node->parent = parent;
      MapNode* raw = node.get();
      parent->children.push_back(std::move(node));
      if (!instance.empty() && !expand_prefab(*raw, instance, section.line)) {
        return false;
      }
      ReadNodeAttributes(section.attributes, *raw);
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
      if (auto missing = resolve_ext(component.properties); !missing.empty()) {
        return fail(section.line,
                    std::format("ExtResource(\"{}\") is not declared", missing));
      }
      // Inside a prefab instance a component section overrides the prefab's
      // component of that type (the index-th one) instead of adding one.
      if (HasPrefabBase(*node)) {
        const int index = static_cast<int>(section.attributes.GetInt("index", 0));
        if (ComponentData* target = node->FindComponent(type, index)) {
          if (section.attributes.GetBool("removed", false)) {
            std::erase_if(node->components,
                          [&](const ComponentData& c) { return &c == target; });
            continue;
          }
          for (auto& property : component.properties) {
            target->properties.Set(property.key, property.value);
          }
          continue;
        }
        if (section.attributes.GetBool("removed", false)) continue;
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

bool Map::Load(const std::filesystem::path& path, std::string* error,
               const PrefabLoaderFn& prefabs) {
  auto text = os::ReadText(path);
  if (!text) {
    if (error) *error = "could not open " + path.string();
    return false;
  }
  std::string inner_error;
  if (!LoadFromString(*text, &inner_error, prefabs)) {
    if (error) *error = path.string() + ":" + inner_error;
    return false;
  }
  return true;
}

std::string Map::SaveToString(const ResourceTypeFn& resource_type) const {
  // Build the node and component sections first, then number the resources
  // they use.
  struct PendingSection {
    std::string tag;
    mtext::PropertyList attributes;
    mtext::PropertyList properties;
  };
  std::vector<PendingSection> pending;

  auto write_node_attributes = [](const MapNode& node, mtext::PropertyList& attributes,
                                  bool only_changes) {
    const bool has_base = only_changes && HasPrefabBase(node);
    const std::string base_tag = has_base ? node.prefab_base_tag : "";
    const int base_layer = has_base ? node.prefab_base_layer : 0;
    const bool base_active = has_base ? node.prefab_base_active : true;
    if (node.tag != base_tag) attributes.Set("tag", node.tag);
    if (node.layer != base_layer) attributes.Set("layer", node.layer);
    if (node.active != base_active) attributes.Set("active", node.active);
  };

  Visit([&](const MapNode& node) {
    const std::string path = node.GetPath();
    if (!node.from_prefab) {
      PendingSection section{"node"};
      section.attributes.Set("name", node.name);
      if (node.parent) section.attributes.Set("parent", node.parent->GetPath());
      if (!node.prefab.empty()) {
        section.attributes.Set("instance", mtext::Resource{node.prefab});
      }
      write_node_attributes(node, section.attributes, true);
      pending.push_back(std::move(section));
    } else {
      // Created by a prefab: only attribute overrides need a header.
      PendingSection section{"node"};
      section.attributes.Set("name", node.name);
      section.attributes.Set("parent", node.parent->GetPath());
      write_node_attributes(node, section.attributes, true);
      if (section.attributes.size() > 2) pending.push_back(std::move(section));
    }

    if (!HasPrefabBase(node)) {
      for (const auto& component : node.components) {
        PendingSection section{"component"};
        section.attributes.Set("type", component.type);
        section.attributes.Set("node", path);
        section.properties = component.properties;
        pending.push_back(std::move(section));
      }
      return;
    }

    // Overrides of a prefab's components: changed keys only.
    std::map<std::string, int> seen;  // type -> count so far
    for (const auto& component : node.components) {
      const int index = seen[component.type]++;
      const ComponentData* base = nullptr;
      int base_index = 0;
      for (const auto& candidate : node.prefab_base) {
        if (candidate.type == component.type && base_index++ == index) {
          base = &candidate;
          break;
        }
      }
      PendingSection section{"component"};
      section.attributes.Set("type", component.type);
      section.attributes.Set("node", path);
      if (index > 0) section.attributes.Set("index", index);
      if (!base) {
        section.properties = component.properties;
        pending.push_back(std::move(section));
        continue;
      }
      for (const auto& property : component.properties) {
        const mtext::Variant* old = base->properties.Get(property.key);
        if (!old || !(*old == property.value)) {
          section.properties.Set(property.key, property.value);
        }
      }
      if (!section.properties.empty()) pending.push_back(std::move(section));
    }
    // Prefab components that were removed.
    std::map<std::string, int> base_seen;
    for (const auto& base : node.prefab_base) {
      const int index = base_seen[base.type]++;
      if (index < seen[base.type]) continue;
      PendingSection section{"component"};
      section.attributes.Set("type", base.type);
      section.attributes.Set("node", path);
      if (index > 0) section.attributes.Set("index", index);
      section.attributes.Set("removed", true);
      pending.push_back(std::move(section));
    }
  });

  mtext::Document document;
  document.Add("mmap").attributes.Set("format", kMapFormatVersion);

  // Number the referenced resources in the order they first appear.
  std::vector<std::string> resource_order;
  std::map<std::string, std::string> resource_ids;
  auto note_resource = [&](const mtext::Variant& value) {
    auto resource = value.Get<mtext::Resource>();
    if (!resource || resource->path.empty()) return;
    if (resource_ids.contains(resource->path)) return;
    resource_ids[resource->path] = std::to_string(resource_order.size() + 1);
    resource_order.push_back(resource->path);
  };
  for (const auto& section : pending) {
    if (auto instance = section.attributes.Get("instance")) note_resource(*instance);
    ForEachValue(section.properties, note_resource);
  }
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

  auto to_ext = [&](mtext::Variant& value) {
    auto resource = value.Get<mtext::Resource>();
    if (!resource || resource->path.empty()) return;
    value = mtext::Variant(mtext::ExtResource{resource_ids[resource->path]});
  };
  for (auto& item : pending) {
    auto& section = document.Add(item.tag);
    section.attributes = std::move(item.attributes);
    if (auto instance = section.attributes.Get("instance")) to_ext(*instance);
    section.properties = std::move(item.properties);
    ForEachValue(section.properties, to_ext);
  }
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

namespace {

MapNode* FindUidBelow(MapNode& node, uint64_t uid) {
  if (node.uid == uid) return &node;
  for (auto& child : node.children) {
    if (MapNode* found = FindUidBelow(*child, uid)) return found;
  }
  return nullptr;
}

}  // namespace

MapNode* Map::FindByUid(uint64_t uid) {
  return root_ ? FindUidBelow(*root_, uid) : nullptr;
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
  auto rewrite = [&](mtext::Variant& value) {
    auto path = value.Get<mtext::NodePath>();
    if (!path) return;
    if (path->path == old_prefix) {
      path->path = new_prefix;
    } else if (path->path.starts_with(old_prefix + "/")) {
      path->path = new_prefix + path->path.substr(old_prefix.size());
    }
  };
  Visit([&](MapNode& node) {
    for (auto& component : node.components) {
      ForEachValue(component.properties, rewrite);
    }
    for (auto& component : node.prefab_base) {
      ForEachValue(component.properties, rewrite);
    }
  });
}

std::unique_ptr<MapNode> Map::CloneTree(const MapNode& node, MapNode* parent) {
  Map scratch;
  return scratch.CloneNode(node, parent);
}

void Map::RebaseNodePaths(MapNode& node, const std::string& instance_path) {
  auto rebase = [&](mtext::Variant& value) {
    auto path = value.Get<mtext::NodePath>();
    if (!path || path->path.empty()) return;
    if (path->path == ".") {
      path->path = instance_path;
    } else if (instance_path != ".") {
      path->path = instance_path + "/" + path->path;
    }
  };
  std::function<void(MapNode&)> walk = [&](MapNode& n) {
    for (auto& component : n.components) ForEachValue(component.properties, rebase);
    for (auto& child : n.children) walk(*child);
  };
  walk(node);
}

MapNode* Map::AddPrefabInstance(MapNode* parent, std::string_view name,
                                const std::string& prefab_path,
                                const Map& prefab) {
  MapNode* node = AddNode(parent, name);
  const MapNode& source_root = *prefab.GetRoot();
  node->prefab = prefab_path;
  node->components = source_root.components;
  node->tag = source_root.tag;
  node->layer = source_root.layer;
  node->active = source_root.active;
  for (const auto& child : source_root.children) {
    auto copy = CloneNode(*child, node);
    AssignUids(*copy);
    node->children.push_back(std::move(copy));
  }
  for (auto& child : node->children) MarkFromPrefab(*child);
  RebaseNodePaths(*node, node->GetPath());
  RememberPrefabBase(*node);
  return node;
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
