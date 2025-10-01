#include "../include/Engine/Core.h"
#include "../include/Engine/Engine.h" // Needed for SetupSimpleScene
#include "../include/Engine/Renderer.h" // Needed for Renderer::Render
#include "../include/Engine/EngineDefines.h" // Needed for ENGINE_LOG
// Explicitly include systems for use with std::make_unique
#include "../include/Engine/ECS/Systems/CameraSystem.h"
#include "../include/Engine/ECS/Systems/HierarchySystem.h"
#include "../include/Engine/ECS/Systems/RenderingSystem.h"
#include "../include/Engine/ECS/Systems/TestObjectMoverSystem.h"
#include "../include/Engine/ECS/Systems/ViewportCameraSystem.h"

// Initialize the static World pointer (must be defined here in Core.cpp)
std::unique_ptr<World> Engine::Core::s_World = nullptr; 

// NOTE: You must update include/Engine/Core.h to declare this static member:
// private: static std::unique_ptr<World> s_World;
// public: static void Init(uint32_t textureID);
// public: static void Update(float deltaTime);

/**
     * @brief Reads a template file into a string.
     * @param templatePath Path to the .template file.
     * @return The content of the file, or an empty string on failure.
     */
std::string ReadTemplate(const std::filesystem::path& templatePath) {
    std::ifstream templateFile(templatePath);
    if (!templateFile.is_open()) {
        LOG_ERROR("Failed to open template file: " + templatePath.string());
        return "";
    }
    std::stringstream buffer;
    buffer << templateFile.rdbuf();
    return buffer.str();
}

/**
 * @brief Performs simple string replacement within the content.
 */
void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' contains 'from'
    }
}

/**
 * @brief Writes content to a file.
 */
bool WriteFile(const std::filesystem::path& filePath, const std::string& content) {
    // Ensure the directory exists
    if (!std::filesystem::exists(filePath.parent_path())) {
        std::filesystem::create_directories(filePath.parent_path());
    }

    std::ofstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open file for writing: " + filePath.string());
        return false;
    }
    file << content;
    file.close();
    return true;
}

void Engine::Core::Init(bool autoRegister) {
    // 1. Create the ECS World instance (Moved from main.cpp)
    s_World = std::make_unique<World>();
    LOG_INFO("Initializing Core Game Logic...");


    if (autoRegister)
        RegisterSystems(s_World.get());
    // 3. Setup Scene/Entities (Moved from main.cpp)
    //Engine::SetupSimpleScene(s_World.get(), 0);
    s_World->UpdateSystems(0, System::SystemRole::Shared);

    LOG_INFO("Core Game Logic Initialized.");
}

void Engine::Core::Update(const float deltaTime, const System::SystemRole currentMode) {
    // CRITICAL: Run all ECS systems with the calculated deltaTime
    if (s_World) {
        s_World->UpdateSystems(deltaTime, currentMode);
    }
}

void Engine::Core::Shutdown() {
    LOG_INFO("Shutting down Core Game Logic...");
    s_World.reset(); // Safely destroy the world and all entities/systems
    LOG_INFO("Core Game Logic Shut down.");
}

bool Engine::Core::SaveWorld(const std::string &filePath) {
    if (!s_World) {
        LOG_ERROR("Cannot save, no world exists!");
        return false;
    }

    return s_World->SaveToJson(filePath+"World.json");
}

bool Engine::Core::LoadWorld(const std::string &filePath) {
    return s_World->LoadFromJson(filePath);
}

bool Engine::Core::AutoGenerateComponent(
    const std::string& componentName,
    const std::filesystem::path& headerDirectory
) {
    const std::filesystem::path templatePath = "../templates/Component.h.template";
    std::string content = ReadTemplate(templatePath);

    if (content.empty()) {
        return false;
    }

    // Replace placeholders
    ReplaceAll(content, "[[COMPONENT_NAME]]", componentName);
    // Include guards are not strictly needed with #pragma once, but this is here for completeness
    // ReplaceAll(content, "[[INCLUDE_GUARD]]", ToUpperSnakeCase(componentName));

    // Write the new file
    const std::filesystem::path filePath = headerDirectory / (componentName + ".h");
    if (WriteFile(filePath, content)) {
        LOG_INFO("Generated Component Header: " + filePath.string());
        return true;
    }
    return false;
}

// =========================================================================
// System Generation
// =========================================================================

bool Engine::Core::AutoGenerateSystem(
    const std::string& systemName,
    const std::filesystem::path& headerDirectory,
    const std::filesystem::path& sourceDirectory
) {
    bool success = true;

    // --- 1. Generate Header (.h) ---
    const std::filesystem::path h_templatePath = "templates/System.h.template";
    std::string h_content = ReadTemplate(h_templatePath);

    if (h_content.empty()) {
        return false;
    }

    ReplaceAll(h_content, "[[SYSTEM_NAME]]", systemName);

    const std::filesystem::path headerPath = headerDirectory / (systemName + ".h");
    if (WriteFile(headerPath, h_content)) {
        LOG_INFO("Generated System Header: " + headerPath.string());
    } else {
        success = false;
    }

    // --- 2. Generate Source (.cpp) ---
    const std::filesystem::path cpp_templatePath = "templates/System.cpp.template";
    std::string cpp_content = ReadTemplate(cpp_templatePath);

    if (cpp_content.empty()) {
        // Log a warning but continue if the header succeeded
        LOG_WARN("Could not read system CPP template.");
    } else {
        const std::filesystem::path headerPathLocal = std::filesystem::relative(headerDirectory, sourceDirectory);

        ReplaceAll(cpp_content, "[[SYSTEM_NAME]]", systemName);
        ReplaceAll(cpp_content, "[[HEADER_PATH]]", headerPathLocal.generic_string());

        const std::filesystem::path sourcePath = sourceDirectory / (systemName + ".cpp");
        if (WriteFile(sourcePath, cpp_content)) {
            LOG_INFO("Generated System Source: " + sourcePath.string());
        } else {
            success = false;
        }
    }

    return success;
}