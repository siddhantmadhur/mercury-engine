/*
 * map.h
 *
 * In-memory model of a .mmap file: a tree of nodes (Unity's GameObjects),
 * each holding an ordered list of components with their properties.
 *
 * The editor edits this model and the runtime (MapScene) instantiates it into
 * entities. Component types and properties the engine does not know about are
 * kept as plain data, so a map never loses information when it is opened by a
 * tool that does not have the game's scripts.
 *
 * File layout (Godot .tscn style):
 *
 *   [mmap format=1]
 *
 *   [ext_resource type="Material" path="res://assets/white.masset" id="1"]
 *
 *   [node name="Pong"]
 *
 *   [node name="Ball" parent="."]
 *
 *   [component type="Transform" node="Ball"]
 *   position = Vector3(0, 0, 0)
 *
 *   [component type="SpriteRenderer" node="Ball"]
 *   material = ExtResource("1")
 *
 * `parent` and `node` are paths from the root node: "." is the root, "Ball"
 * is a child of the root and "Walls/Left" a grandchild. In memory, asset
 * references are kept as Resource("res://...") values; ExtResource ids are
 * only used inside the file.
 */
#ifndef MERCURY_PROJECT_MAP_H
#define MERCURY_PROJECT_MAP_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "serialization/mtext.h"

namespace Mercury {

constexpr int kMapFormatVersion = 1;

struct ComponentData {
  std::string type;
  mtext::PropertyList properties;
};

struct MapNode {
  std::string name;
  MapNode* parent = nullptr;
  std::vector<std::unique_ptr<MapNode>> children;
  std::vector<ComponentData> components;
  // Unique within its Map for the lifetime of the process; not saved. Used to
  // keep editor selection and entities attached across edits.
  uint64_t uid = 0;

  ComponentData* FindComponent(std::string_view type);
  const ComponentData* FindComponent(std::string_view type) const;
  bool IsRoot() const { return parent == nullptr; }
  bool IsAncestorOf(const MapNode* node) const;
  int IndexInParent() const;

  // "." for the root, otherwise "Child/Grandchild"
  std::string GetPath() const;
};

class Map {
 public:
  // Given a res:// path, returns the asset type to write in [ext_resource].
  using ResourceTypeFn = std::function<std::string(const std::string&)>;

  Map();  // a map with a single root node called "Root"
  Map(const Map& other);
  Map& operator=(const Map& other);

  bool LoadFromString(std::string_view source, std::string* error = nullptr);
  bool Load(const std::filesystem::path& path, std::string* error = nullptr);

  std::string SaveToString(const ResourceTypeFn& resource_type = {}) const;
  bool Save(const std::filesystem::path& path,
            const ResourceTypeFn& resource_type = {}) const;

  MapNode* GetRoot() { return root_.get(); }
  const MapNode* GetRoot() const { return root_.get(); }

  MapNode* FindNode(std::string_view path);
  const MapNode* FindNode(std::string_view path) const;
  MapNode* FindByUid(uint64_t uid);

  /**
   * @brief Add a child node; the name is made unique among its siblings
   * @param index Position among the siblings, or -1 to append
   */
  MapNode* AddNode(MapNode* parent, std::string_view name, int index = -1);

  // Removes the node and its children. The root cannot be removed.
  bool RemoveNode(MapNode* node);

  // Copies the node (and its children) next to the original.
  MapNode* DuplicateNode(MapNode* node);

  /**
   * @brief Move a node under a new parent
   *
   * Fails if that would make the node its own ancestor. NodePath properties
   * that pointed into the moved subtree are updated.
   */
  bool ReparentNode(MapNode* node, MapNode* new_parent, int index = -1);

  /**
   * @brief Rename a node, keeping sibling names unique
   *
   * NodePath properties that point at the node or its children are updated.
   * @return The name actually used
   */
  std::string RenameNode(MapNode* node, std::string_view new_name);

  std::string UniqueChildName(const MapNode* parent, std::string_view base,
                              const MapNode* ignore = nullptr) const;

  // Pre-order walk (parents before children), the same order as the file.
  void Visit(const std::function<void(MapNode&)>& fn);
  void Visit(const std::function<void(const MapNode&)>& fn) const;

  size_t NodeCount() const;

 private:
  static const MapNode* FindFrom(const MapNode* root, std::string_view path);
  std::unique_ptr<MapNode> CloneNode(const MapNode& node, MapNode* parent);
  void AssignUids(MapNode& node);
  // Rewrites NodePath values starting with old_prefix to start with
  // new_prefix instead.
  void RewriteNodePaths(const std::string& old_prefix,
                        const std::string& new_prefix);

  std::unique_ptr<MapNode> root_;
  // Asset types seen while loading, reused when saving.
  std::map<std::string, std::string> resource_types_;
};

// Strips characters that cannot appear in a node name ('/', '.', quotes).
std::string SanitizeNodeName(std::string_view name);

}  // namespace Mercury

#endif  // MERCURY_PROJECT_MAP_H
