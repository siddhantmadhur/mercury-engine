#include <format>
#include "editor_project.hh"
#include "debugger/debugger.h"
#include <editor_windows/editor_scene.hh>
#include <project_handler/project_handler.hh>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <string>
#include <cstring>
#include <stdlib.h>
#include <filesystem>

#define STR_LEN 128

#define AUTO_LOAD_PONG

namespace fs = std::filesystem;


namespace Pequod {
    void ProjectSelectionScene::OnInitialLoad() {
        this->new_project_name = new char[STR_LEN]{};
        strcpy(new_project_name, "hello_world");
        this->project_directory = new char[STR_LEN]{};
        const char *home_dir_env = getenv("HOME");
        if (home_dir_env) {
            strcpy(project_directory, home_dir_env);
            strcat(project_directory, "/pequod/");
        }

        if (home_dir_env) {
            current_project_handler = std::make_unique<ProjectHandler>(ProjectHandler(home_dir_env));

#ifdef AUTO_LOAD_PONG
            current_project_handler->ReloadProjects();
            auto proj = current_project_handler->GetProjects();
            if (!proj.empty()) {
                current_project_handler->Load(proj[0]);
                OpenProject();
            }
#endif
        }
    }

    static void imgui_callback(const ImDrawList *dl, const ImDrawCmd *cmd) {
        WorldScene **scene_ptr = (WorldScene **) cmd->UserCallbackData;
        if (!scene_ptr) {
            return;
        }
        WorldScene *cur_scene = *scene_ptr;
        if (!cur_scene) {
            return;
        }

        const int cx = (int) cmd->ClipRect.x;
        const int cy = (int) cmd->ClipRect.y;
        const int cw = (int) (cmd->ClipRect.z - cmd->ClipRect.x);
        const int ch = (int) (cmd->ClipRect.w - cmd->ClipRect.y);
        sg_apply_scissor_rect(cx, cy, cw, ch, true);
        // TODO: remove this debug, issue resolved
#define DEBUG_CENTER_PREVIEW 1
#if DEBUG_CENTER_PREVIEW
        if ((cw / 16.0) < (ch / 9.0)) {
            float offset_y = (ch - (cw * (9.0 / 16.0f))) / 2.0f;
            sg_apply_viewport(cx, cy + offset_y, cw, cw * (9.0 / 16.0), true);
        } else {
            float offset_x = (cw - (ch * (16.0 / 9.0))) / 2.0f;
            sg_apply_viewport(cx + offset_x, cy, ch * (16.0 / 9.0), ch, true);
        }
#else
        sg_apply_viewport(cx , cy, cw, ch , true);
#endif

        cur_scene->SetupRenderState();
        cur_scene->RenderObjects();
    }

    void ProjectSelectionScene::RenderScenePreview(WorldScene **scene) {
        ImGui::Begin("GamePreview");
        auto *dl = ImGui::GetWindowDrawList();
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
        dl->AddCallback(imgui_callback, scene);
        ImGui::End();
    }

    void ProjectSelectionScene::OnEventUpdate(const sapp_event *event) {
        if (event->type == SAPP_EVENTTYPE_KEY_DOWN) {
            if ((event->modifiers & SAPP_MODIFIER_CTRL) && event->key_code == SAPP_KEYCODE_S) {
                SaveScene();
            }
        }
    }

    void ProjectSelectionScene::QuitProgram() {
        sapp_quit();
    }

    void ProjectSelectionScene::CreateProject() {
        if (current_project_handler) {
            current_project_handler->Save();
        }

        PDebug::info("Creating new project: {}", new_project_name);

        current_project_handler->Create(new_project_name);
        show_new_project_window = false;
    }

    void ProjectSelectionScene::ToggleNewProjectWin() {
        show_new_project_window = true;
    }

    void ProjectSelectionScene::ToggleOpenProjectWin() {
        open_prev_project_window = true;
    }

    void ProjectSelectionScene::OpenProject() {
        current_project_handler->ReloadProjects();
        explorer.SetRootDirectory(current_project_handler->project_path);
        explorer.Initialize();
        object_tree = std::make_unique<ObjectTree>(current_project_handler->project_path, this->object_manager,
                                                   this->selected_entity);
        object_tree->Initialize();

        object_properties = std::make_unique<ObjectPropertiesPanel>(this->selected_entity, this->object_manager);
        object_properties->Initialize();
    }

    void ProjectSelectionScene::SaveScene() {
    }


    void ProjectSelectionScene::OnFrameUpdate() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                    ToggleNewProjectWin();
                }
                if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                    ToggleOpenProjectWin();
                }
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    SaveScene();
                }
                ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);
                if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                    QuitProgram();
                }


                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Windows")) {
                if (ImGui::MenuItem("FileExplorer", "", explorer.IsShown())) {
                    explorer.Toggle();
                }
                if (object_tree) {
                    if (ImGui::MenuItem("ObjectTree", "", object_tree->IsShown())) {
                        object_tree->Toggle();
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

        if (show_new_project_window) {
            //ImGui::SetNextWindowSize({360.0f, 128.0f});
            //ImGui::SetNextWindowPos({sapp_widthf() / 2.0f, sapp_heightf() / 2.0f}, 0, {0.5f, 0.5f});
            if (ImGui::Begin(
                "Create New Project", &show_new_project_window,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoDocking
            )) {
                ImGui::InputText("Project Name", new_project_name, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr,
                                 nullptr);
                ImGui::InputText("Directory", project_directory, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr,
                                 nullptr);
                if (ImGui::Button("Create")) {
                    CreateProject();
                }
                ImGui::End();
            }
        }

        if (open_prev_project_window) {
            //ImGui::SetNextWindowSize({320.0f, 128.0f});
            if (ImGui::Begin(
                "Open Project", &open_prev_project_window,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoDocking
            )) {
                auto projects = current_project_handler->GetProjects();

                static int selected_index = 0;

                ImGui::BeginChild("Projects", {300.0f, 80.0f});
                for (int i = 0; i < projects.size(); i++) {
                    const bool is_selected = selected_index == i;
                    if (ImGui::Selectable(reinterpret_cast<const char*>(projects[i].c_str()), is_selected)) {
                        selected_index = i;
                    }

                    if (selected_index) {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndChild();

                if (ImGui::Button("Open")) {
                    current_project_handler->Load(projects[selected_index]);
                    open_prev_project_window = false;
                    show_new_project_window = false;
                }

                ImGui::End();
            }
        }


        if (current_project_handler->has_loaded_project) {
            // Show editor here basically


            ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);

            explorer.Draw();
            //game_preview.Draw();

            if (object_tree && object_tree->IsShown()) {
                object_tree->Draw();
            }

            if (object_properties && object_properties->IsShown()) {
                object_properties->Draw();
            }
            {
                ImGui::Begin("TilesetEditor");
                ImGui::End();
            }
            {
                ImGui::Begin("Audio");
                ImGui::End();
            }
            {
                ImGui::Begin("Console");
                ImGui::End();
            }
        }
    }

    void ProjectSelectionScene::OnTickUpdate() {
        if (IsKeyPressed(SAPP_KEYCODE_LEFT_CONTROL) || IsKeyPressed(SAPP_KEYCODE_RIGHT_CONTROL)) {
            if (IsKeyPressed(SAPP_KEYCODE_N)) {
                ToggleNewProjectWin();
            } else if (IsKeyPressed(SAPP_KEYCODE_Q)) {
                QuitProgram();
            } else if (IsKeyPressed(SAPP_KEYCODE_O)) {
                ToggleOpenProjectWin();
            }
        }
    }

    void ProjectSelectionScene::OnDestroy() {
        delete new_project_name;
        delete project_directory;
    }
}
