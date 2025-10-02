// --- MyGameProject Application Entry Point ---
#include <filesystem>
#include <iostream>

#include "Engine.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "ECS/World.h"
#include "Input.h"
#include "Renderer.h" // Need access to Engine::Renderer::Render()

// Forward declarations of Systems are no longer needed, as they are managed by Engine::Core

GLFWwindow* g_MainWindow = nullptr;

int main() {

    if (!glfwInit()) {
        std::cerr << "FATAL: GLFW failed to initialize.\n";
        return -1;
    }

    // --- FIX 1: Make Window Non-Resizable (As requested) ---
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "MyGameProject", nullptr, nullptr);
    if (!window) {
        std::cerr << "FATAL: GLFW failed to create window.\n";
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

    Engine::Initialize(Engine::Network::Role::None, "0.0.0.0", 7777);

    // --- MAIN GAME LOOP (EDITOR-STYLE BLIT) ---

    // NOTE: This assumes the engine's internal FBO ID is 1.
    // Check Renderer::Init() to confirm the value of s_Data->EditorFBO if this fails.
    const uint32_t GAME_FBO_ID = 1;

    int display_w = 1280, display_h = 720;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 1. INPUT & VIEWPORT PHASE
        Engine::Input::UpdateState();

        glfwGetFramebufferSize(window, &display_w, &display_h);
        Engine::Renderer::SetViewportSize(static_cast<float>(display_w), static_cast<float>(display_h));

        // 2. RENDER GAME TO FBO
        // Renderer::Render() binds FBO and clears it.
        Engine::Renderer::Render();

        // Engine::Update() runs ECS systems (submitting geometry).
        Engine::Update(System::SystemRole::Game);

        // Flushes geometry to FBO. EndBatch() also UNBINDS the FBO (target is now 0).
        Engine::Renderer::EndBatch();

        // 3. CLEAR WINDOW BUFFER (DEFAULT FRAMEBUFFER)
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, display_w, display_h);

        // Clear the window with the desired background color.
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Use your desired background color here
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 4. BLIT FBO TO WINDOW (Copies geometry onto cleared background)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, GAME_FBO_ID);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0); // 0 is the default window buffer

        // Copy the color buffer from the FBO (Source) to the window (Destination)
        glBlitFramebuffer(
            0, 0, display_w, display_h, // Source rectangle
            0, 0, display_w, display_h, // Destination rectangle
            GL_COLOR_BUFFER_BIT,        // Buffer to copy (color)
            GL_NEAREST                  // Filtering (fastest)
        );

        // Reset binding for final swap
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 5. PRESENT
        glfwSwapBuffers(window);
    }

    // --- Engine Shutdown ---
    Engine::Shutdown();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}