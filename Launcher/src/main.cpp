#include "launcher.h"

#include <zues/project.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace Engine::launcher;

namespace {
    constexpr int LAUNCHER_W = 720;
    constexpr int LAUNCHER_H = 480;

    bool launch_project(const std::string& path,
                        const std::string& display_name,
                        std::vector<RecentEntry>& recents,
                        std::string& err) {
        if (!spawn_editor(path)) {
            err = "Failed to launch editor (editor.exe not found?)";
            return false;
        }
        promote_recent(recents, path, display_name);
        save_recents(recents);
        return true;
    }
}

int main(int /*argc*/, char** /*argv*/) {
    glfwSetErrorCallback([](int code, const char* desc) {
        std::fprintf(stderr, "launcher GLFW error %d: %s\n", code, desc ? desc : "");
    });
    if (!glfwInit()) {
        std::fprintf(stderr, "launcher: glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* w = glfwCreateWindow(LAUNCHER_W, LAUNCHER_H,
                                     "Zues Engine — Launcher", nullptr, nullptr);
    if (!w) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(w);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplGlfw_InitForOpenGL(w, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    apply_theme();

    auto recents = load_recents();
    bool exit_after_launch = false;
    std::string error_msg;

    // New-project modal state.
    bool   show_new_project = false;
    char   np_name[128]     = "MyGame";
    char   np_dir [512]     = "";

    // Build-error modal state.
    bool        show_build_error = false;
    std::string build_error_output;

    // Headless / CI: env override to skip the GUI loop.
    int frame_cap = 0;
    if (const char* env = std::getenv("ZUES_FRAME_CAP")) frame_cap = std::atoi(env);
    int frames = 0;

    while (!glfwWindowShouldClose(w) && !exit_after_launch
           && (frame_cap == 0 || frames < frame_cap)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ---- main launcher window ------------------------------------------
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos (vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);

        constexpr ImGuiWindowFlags WF =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize   | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        if (ImGui::Begin("Launcher", nullptr, WF)) {
            ImGui::PushFont(nullptr);
            ImGui::TextUnformatted("Zues Engine");
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("v0.1.0");

            ImGui::Spacing();
            if (ImGui::Button("Open Project...", ImVec2(160, 32))) {
                std::string p;
                if (pick_open_file(p, "Open Project", "Zues Project", "*.zuesproject")) {
                    Engine::Project proj;
                    if (Engine::load_project(p.c_str(), proj) == Engine::Result::Ok) {
                        if (launch_project(p, proj.name, recents, error_msg)) {
                            exit_after_launch = true;
                        }
                    } else {
                        error_msg = "Couldn't read project file: " + p;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("New Project...", ImVec2(160, 32))) {
                show_new_project = true;
                std::strncpy(np_dir, fs::current_path().string().c_str(), sizeof(np_dir) - 1);
                np_dir[sizeof(np_dir) - 1] = '\0';
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Recent");

            if (recents.empty()) {
                ImGui::TextDisabled("  (no recent projects)");
            } else {
                for (size_t i = 0; i < recents.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    char label[256];
                    std::snprintf(label, sizeof(label), "%s##recent_%zu",
                                  recents[i].name.c_str(), i);
                    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(0) || true) {
                            const std::string path = recents[i].path;
                            const std::string name = recents[i].name;
                            if (launch_project(path, name, recents, error_msg)) {
                                exit_after_launch = true;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", recents[i].path.c_str());
                    ImGui::PopID();
                }
            }

            if (!error_msg.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", error_msg.c_str());
            }
        }
        ImGui::End();

        // ---- new-project modal ---------------------------------------------
        if (show_new_project) {
            ImGui::OpenPopup("New Project");
            show_new_project = false;
        }
        if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Create a new Zues project");
            ImGui::Separator();

            ImGui::InputText("Name",          np_name, sizeof(np_name));
            ImGui::InputText("Parent folder", np_dir,  sizeof(np_dir));
            ImGui::SameLine();
            if (ImGui::Button("Browse...##nd")) {
                std::string picked;
                if (pick_folder(picked, "Select project parent folder")) {
                    std::strncpy(np_dir, picked.c_str(), sizeof(np_dir) - 1);
                    np_dir[sizeof(np_dir) - 1] = '\0';
                }
            }

            const fs::path full = fs::path(np_dir) / np_name;
            ImGui::TextDisabled("Will create: %s", full.string().c_str());
            ImGui::Spacing();

            const bool valid = np_name[0] != '\0' && np_dir[0] != '\0';

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button("Create + Open", ImVec2(140, 0))) {
                if (!create_lync_skeleton(full, np_name)) {
                    error_msg = "Failed to create project at: " + full.string();
                    ImGui::CloseCurrentPopup();
                } else {
                    std::string build_out;
                    if (!run_build_sync(full, build_out)) {
                        build_error_output = build_out;
                        show_build_error   = true;
                        ImGui::CloseCurrentPopup();
                    } else {
                        fs::path proj_file = full / (std::string(np_name) + ".zuesproject");
                        if (launch_project(proj_file.string(), np_name, recents, error_msg))
                            exit_after_launch = true;
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- build-error modal ---------------------------------------------
        if (show_build_error) {
            ImGui::OpenPopup("Build Failed");
            show_build_error = false;
        }
        if (ImGui::BeginPopupModal("Build Failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "First-time build failed.");
            ImGui::Separator();
            ImGui::TextUnformatted("Project was created but the DLL could not be compiled.");
            ImGui::Spacing();
            ImGui::TextDisabled("Output:");
            ImGui::InputTextMultiline("##build_out",
                const_cast<char*>(build_error_output.c_str()),
                build_error_output.size() + 1,
                ImVec2(560, 200),
                ImGuiInputTextFlags_ReadOnly);
            ImGui::Spacing();
            if (ImGui::Button("OK", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // ---- render --------------------------------------------------------
        ImGui::Render();
        int dw = 0, dh = 0;
        glfwGetFramebufferSize(w, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(w);
        ++frames;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(w);
    glfwTerminate();
    return 0;
}
