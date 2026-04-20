//
// Created by smadhur on 3/30/26.
//

#ifndef PEQUOD_OBJECT_TREE_H
#define PEQUOD_OBJECT_TREE_H
#include <os/filesystem.h>
#include <pobject/manager.h>

#include <filesystem>

#include "ecs/ecs.hh"
#include "panel.hh"

namespace Pequod {
class ObjectTree : public Panel {
 public:
  ObjectTree(const fs::path &, std::shared_ptr<PObjectManager>, entity_id &);

  ~ObjectTree();

  void Initialize() override;

  void Draw() override;

  void DrawID(entity_id);

 private:
  fs::path project_path;
  std::shared_ptr<PObjectManager> manager = nullptr;
  entity_id &selected_id;
};
}  // namespace Pequod

#endif
