#include "file_explorer.hh"
#include "debugger/debugger.h"
#include "imgui/imgui.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <format>


namespace Pequod {
    FileExplorer::FileExplorer() : Panel(false) {
        const char *home_dir = getenv("HOME");
        if (home_dir) {
            SetRootDirectory(home_dir);
        }
    }

    void FileExplorer::Initialize() {
        this->show_panel = true;
        //PDebug::info(std::format("Root: {}", root_directory.string()));
        ReloadFolders();
    }

    void FileExplorer::DrawFolder(fs::path root) {
        auto arr = this->folder_data[root.string()];
        ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull;
        for (int i = 0; i < arr.size(); i++) {
            auto cur = arr[i];
            if (cur.is_directory()) {
                if (ImGui::TreeNodeEx(cur.path().stem().generic_string().c_str(), base_flags)) {
                    DrawFolder(cur.path());
                    ImGui::TreePop();
                }
            } else {
                ImGui::Text("%s", cur.path().filename().generic_string().c_str());
            }
        }
    }

    void FileExplorer::ReloadFolders() {
        folder_data.clear();
        for (const auto &cur: fs::recursive_directory_iterator(root_directory)) {
            if (!cur.path().string().starts_with(".")) {
                folder_data[cur.path().parent_path().string()].push_back(cur);
            }
        }
    }

    void FileExplorer::Draw() {
        if (show_panel) {
            ImGui::Begin("FileExplorer", &show_panel, 0);
            if (ImGui::BeginPopupContextWindow()) {
                if (ImGui::Button("New File")) {
                }
                ImGui::EndPopup();
            }

            ImGuiTreeNodeFlags base_flags = ImGuiTreeNodeFlags_DrawLinesFull | ImGuiTreeNodeFlags_DefaultOpen;
            if (ImGui::TreeNodeEx("pequod://", base_flags)) {
                ImGui::SameLine();
                if (ImGui::Button("Reload")) {
                    ReloadFolders();
                }
                DrawFolder(root_directory);
                ImGui::TreePop();
            }


            ImGui::End();
        }
    }

    void FileExplorer::SetRootDirectory(fs::path init_directory) {
        root_directory = init_directory;
    }
} // namespace Pequod
