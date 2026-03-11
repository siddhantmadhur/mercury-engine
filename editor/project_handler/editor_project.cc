#include <format>
#include "editor_project.hh"
#include "debugger/debugger.hh"
#include <editor_windows/editor_scene.hh>
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <string>
#include <cstring>
#include <stdlib.h>

#define STR_LEN 128

namespace Pequod {

void ProjectSelectionScene::OnInitialLoad() {
    this->new_project_name =  new char[STR_LEN]{};
    strcpy(new_project_name, "hello_world");
    this->project_directory =  new char[STR_LEN]{};
    const char* home_dir = getenv("HOME");
    if (home_dir) {
        strcpy(project_directory, home_dir);
        strcat(project_directory, "/pequod/");
    }
    this->show_new_project_window = true;

}

void ProjectSelectionScene::OnEventUpdate(const sapp_event* event) {}

void ProjectSelectionScene::QuitProgram() {
    sapp_quit();
}
void ProjectSelectionScene::CreateNewProject() {
    show_new_project_window = true;
}


void ProjectSelectionScene::OnFrameUpdate() {
    if (IsKeyPressed(SAPP_KEYCODE_LEFT_CONTROL) || IsKeyPressed(SAPP_KEYCODE_RIGHT_CONTROL)) {
        if (IsKeyPressed(SAPP_KEYCODE_N)) {
            CreateNewProject();
        }
        if (IsKeyPressed(SAPP_KEYCODE_Q)) {
            QuitProgram();
        }
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                CreateNewProject();
            }
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {
                show_new_project_window = !show_new_project_window;
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
        ImGui::Begin(
            "Create New Project", &show_new_project_window, 
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
        );
        ImGui::InputText("Project Name", new_project_name, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
        ImGui::InputText("Directory", project_directory, STR_LEN, ImGuiInputTextFlags_CharsNoBlank, nullptr, nullptr);
        if (ImGui::Button("Create")) {
            PDebug::info("Create new project");
        } 
        ImGui::End();
    }
}

void ProjectSelectionScene::OnTickUpdate() {
}

void ProjectSelectionScene::OnDestroy() {
    //PDebug::info(std::format("Saving imgui settings to: {}", imgui_configuration_path));
    //ImGui::SaveIniSettingsToDisk(imgui_configuration_path.c_str());
    delete new_project_name;
}

}
