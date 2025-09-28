#include "Engine.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../include/EditorUi.h"
#include "ECS/World.h" // Include World for the pointer type

int main() {

    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Editor", nullptr, nullptr);
    if (!window) return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        return -1;
    }

    Engine::Initialize(Engine::Network::Role::Host, "0.0.0.0", 7777);
    Engine::IEventSystem->Subscribe<Engine::LogEvent>(EditorUi::TestGetLogEvent);

    const uint32_t textureID = Engine::Renderer::LoadTexture("C:/Users/bucka/Pictures/.jpg");
    World* world = new World();

    world->RegisterSystem(std::make_unique<CameraSystem>());
    world->RegisterSystem(std::make_unique<RenderingSystem>());

    Engine::SetupSimpleScene(world, textureID);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. SCENE PREP (Binds FBO 1, Clears it)
        Engine::Renderer::Render();

        // 2. ECS EXECUTION (This was missing!)
        // NOTE: This runs CameraSystem (sets ViewProjection) and RenderingSystem (submits quads).
        world->UpdateSystems(1); // <--- MISSING ECS UPDATE

        // 3. FLUSH THE BATCH (This issues the draw call for all submitted quads to FBO 1)
        Engine::Renderer::EndBatch(); // <--- MISSING DRAW CALL

        // 4. UNBIND FBO (Switches back to the default framebuffer/screen)
        glBindFramebuffer(GL_FRAMEBUFFER, 0); // <--- Final step for scene rendering

        ENGINE_LOG("Some random log message", LOGLEVEL_INFO);

        // Optional: Reset viewport for main window (needed for correct ImGui rendering)
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // 5. UI Rendering
        Engine::Update(); // (If you want to keep the engine-level update here)

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        EditorUi::DrawWindowUi();

        ImGui::Render();
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    Engine::Shutdown();
}
