#include <format>
#include "editor_project.hh"
#include "debugger/debugger.hh"
#include <editor_windows/editor_scene.hh>
#include <project_handler/project_handler.hh>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <string>
#include <cstring>
#include <stdlib.h>

#define STR_LEN 128

#define AUTO_LOAD_PONG 


namespace Pequod {

void ProjectSelectionScene::OnInitialLoad() {
    this->new_project_name =  new char[STR_LEN]{};
    strcpy(new_project_name, "hello_world");
    this->project_directory =  new char[STR_LEN]{};
    const char* home_dir_env = getenv("HOME");
    if (home_dir_env) {
        strcpy(project_directory, home_dir_env);
        strcat(project_directory, "/pequod/");
    }
    
    if (home_dir_env) {
        current_project_handler = std::make_unique<ProjectHandler>(ProjectHandler(home_dir_env));

#ifdef AUTO_LOAD_PONG
        current_project_handler->ReloadProjects();
        auto proj = current_project_handler->GetProjects();
        current_project_handler->Load(proj[0]);

#endif
    }


}

void ProjectSelectionScene::OnEventUpdate(const sapp_event* event) {}

void ProjectSelectionScene::QuitProgram() {
    sapp_quit();
}
void ProjectSelectionScene::CreateNewProject() {
    if (current_project_handler) {
        current_project_handler->Save();
    }

    PDebug::info(std::format("Creating new project: {}", new_project_name));

    current_project_handler->Create(new_project_name);
    show_new_project_window = false;
}

void ProjectSelectionScene::OpenNewProjectWin() {
    show_new_project_window = true;
}

void ProjectSelectionScene::OpenPrevProjectWin() {
    open_prev_project_window = true;
    current_project_handler->ReloadProjects();
}


void ProjectSelectionScene::OnFrameUpdate() {

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                OpenNewProjectWin();
            }
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                OpenPrevProjectWin();
            }
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                QuitProgram();
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
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking
        )) {
            ImGui::InputText("Project Name", new_project_name, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
            ImGui::InputText("Directory", project_directory, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
            if (ImGui::Button("Create")) {
                CreateNewProject();
            } 
            ImGui::End();
        }
    }

    if (open_prev_project_window) {
        //ImGui::SetNextWindowSize({320.0f, 128.0f});
        if (ImGui::Begin(
            "Open Project", &open_prev_project_window, 
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking
        )) {
            auto projects = current_project_handler->GetProjects();
            
            static int selected_index = 0;

            ImGui::BeginChild("Projects", {300.0f, 80.0f});
            for (int i = 0; i < projects.size(); i++) {
                const bool is_selected = selected_index == i;
                if (ImGui::Selectable(projects[i].c_str(), is_selected)) {
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


    if (current_project_handler->has_loaded_project) { // Show editor here basically
       


        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);

        if (ImGui::Begin("File Explorer")) {
            ImGui::End();
        }
        if (ImGui::Begin("Project Tree")) {
            ImGui::End();
        }
        if (ImGui::Begin("Object Properties")) {
            ImGui::End();
        }
        

    }
}

void ProjectSelectionScene::OnTickUpdate() {
    if (IsKeyPressed(SAPP_KEYCODE_LEFT_CONTROL) || IsKeyPressed(SAPP_KEYCODE_RIGHT_CONTROL)) {
        if (IsKeyPressed(SAPP_KEYCODE_N)) {
            OpenNewProjectWin();
        }
        if (IsKeyPressed(SAPP_KEYCODE_Q)) {
            QuitProgram();
        }
        if (IsKeyPressed(SAPP_KEYCODE_O)) {
            OpenPrevProjectWin();
        }
    }
}

void ProjectSelectionScene::OnDestroy() {
    delete new_project_name;
    delete project_directory;
}

}
