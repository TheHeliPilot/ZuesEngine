#include <filesystem>
#include <iostream> // Added for the fatal error message

#include "Engine.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "../include/EditorUi.h"
// #include "ECS/World.h" // No longer needed for pointer type
#include "Input.h"
#include "Renderer.h" // For LoadTexture

// Global pointer for Input handling
extern GLFWwindow* g_MainWindow;
GLFWwindow* g_MainWindow = nullptr;


int main() {

    if (!glfwInit()) return -1;

    // --- Window Creation ---
    // HINT: May need to set GLFW hints for correct context version if not using 150/330 core.
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Editor", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }

    g_MainWindow = window;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable V-Sync

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "FATAL: Failed to initialize GLAD.\n";
        return -1;
    }

    // --- Engine Initialization (Calls Core::Init() and creates the static World) ---
    Engine::Initialize(Engine::Network::Role::Host, "0.0.0.0", 7777);
    Engine::IEventSystem->Subscribe<Engine::LogEvent>(EditorUi::TestGetLogEvent);


    if (!Engine::ProjectManager::OpenOrCreate(EditorUi::projectDir)) {
        // If the project fails to load or create, we can't continue.
        std::cerr << "FATAL: Failed to open or create project at " << EditorUi::projectDir << "\n";
        return -1;
    }

    // Load the texture. The scene entities are created in Core::Init(), but
    // a later fix will be needed to pass this textureID correctly to those entities.
    // For now, the square in the scene will likely default to the white texture (ID 0/1).
    const uint32_t textureID = Engine::Renderer::LoadTexture("assets/bucka.png");


    // ----------------------------------------------------------------------------------
    // FIX: Removed redundant World creation and system registration:
    // World* world = new World();
    // world->RegisterSystem(std::make_unique<CameraSystem>());
    // world->RegisterSystem(std::make_unique<RenderingSystem>());
    // Engine::SetupSimpleScene(world, textureID); // This must run on Core's world, handled in Core::Init()
    // ----------------------------------------------------------------------------------


    // --- ImGui Setup ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // enables multi-platform windowing

    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f; // removes rounding for platform windows
        style.Colors[ImGuiCol_WindowBg].w = 1.0f; // makes background fully opaque
    }

    // Set up viewport for initial sizing
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");


    // --- MAIN LOOP ---

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- 1. GAME UPDATE PHASE ---
        Engine::Input::UpdateState();

        // FIX: The ECS update must happen *after* the FBO is bound,
        // but *before* the batch is finalized (EndBatch).

        // Renderer::Render() binds the Framebuffer (FBO) and clears it.
        Engine::Renderer::Render();

        // Engine::Update() calls Core::Update(deltaTime), which runs the ECS systems
        // (CameraSystem -> updates VP matrix, RenderingSystem -> submits quads to batch).
        Engine::Update(System::SystemRole::Editor);

        // Flushes the accumulated batch data (quads) to the FBO's texture.
        Engine::Renderer::EndBatch();

        // --- 2. EDITOR RENDER PHASE ---

        // Bind the default framebuffer (the window) for ImGui to draw on.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Reset the main viewport to the full window size.
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        // Start the ImGui Frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw the editor UI, which reads the FBO texture and draws it in the viewport panel.
        EditorUi::DrawWindowUi();

        // Render ImGui data
        ImGui::Render();

        // Clear the screen BEFORE drawing the ImGui output.
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // ---- Multi-Viewport / Platform Windows ----
        if (const ImGuiIO& im_gui_io = ImGui::GetIO(); im_gui_io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        // ------------------------------------------

        glfwSwapBuffers(window);
    }

    // --- Cleanup ---
    Engine::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}