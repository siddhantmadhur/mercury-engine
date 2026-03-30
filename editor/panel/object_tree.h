//
// Created by smadhur on 3/30/26.
//

#ifndef PEQUOD_OBJECT_TREE_H
#define PEQUOD_OBJECT_TREE_H
#include <filesystem>

#include "panel.hh"

namespace fs = std::filesystem;

namespace Pequod {
    class ObjectTree : public Panel {
    public:
        ObjectTree(const fs::path&);
        ~ObjectTree();
        void Initialize() override;
        void Draw() override;
    };
}

#endif