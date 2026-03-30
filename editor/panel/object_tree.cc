//
// Created by smadhur on 3/30/26.
//

#include "object_tree.h"

#include "imgui/imgui.h"

namespace Pequod {
    ObjectTree::ObjectTree(const fs::path& dir) : Panel(true) {

    };
    ObjectTree::~ObjectTree() = default;

    void ObjectTree::Initialize() {

    }
    void ObjectTree::Draw() {
        ImGui::Begin("ObjectTree");
        ImGui::End();
    }
}
