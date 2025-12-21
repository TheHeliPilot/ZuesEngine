// GameDLLLoader.cpp - Implementation of game DLL hot-reload system
#include "../include/GameDLLLoader.h"
#include "EngineDefines.h"
#include <thread>
#include <atomic>
#include <fstream>
#include <sstream>

namespace Editor {

    GameDLLLoader& GameDLLLoader::Get() {
        static GameDLLLoader instance;
        return instance;
    }

    GameDLLLoader::~GameDLLLoader() {
        Shutdown();
    }

    bool GameDLLLoader::Initialize(const std::filesystem::path& projectPath) {
        m_ProjectPath = projectPath;

        // The game DLL will be in the project's build directory
        #if defined(_WIN32) || defined(_WIN64)
            m_DLLPath = projectPath / "Builds" / "GameDLL.dll";
        #elif defined(__APPLE__)
            m_DLLPath = projectPath / "Builds" / "libGameDLL.dylib";
        #else
            m_DLLPath = projectPath / "Builds" / "libGameDLL.so";
        #endif

        m_LastSourceCheck = std::chrono::system_clock::now();
        LOG_INFO("GameDLLLoader initialized for project: " + projectPath.string());
        return true;
    }

    void GameDLLLoader::Shutdown() {
        UnloadDLL();
        CleanupTempDLLs();

        if (m_SavedState) {
            free(m_SavedState);
            m_SavedState = nullptr;
            m_SavedStateSize = 0;
        }
    }

    bool GameDLLLoader::LoadDLL() {
        if (m_Status == GameDLLStatus::Loaded) {
            LOG_WARN("Game DLL is already loaded. Call UnloadDLL first.");
            return false;
        }

        if (!std::filesystem::exists(m_DLLPath)) {
            m_LastError = "Game DLL not found: " + m_DLLPath.string();
            m_Status = GameDLLStatus::Error;
            LOG_ERROR(m_LastError);
            return false;
        }

        m_Status = GameDLLStatus::Loading;
        LOG_INFO("Loading game DLL: " + m_DLLPath.string());

        // Copy DLL for loading (allows original to be overwritten during hot-reload)
        if (!CopyDLLForLoading()) {
            m_Status = GameDLLStatus::Error;
            return false;
        }

        // Load the library
        if (!LoadLibrary(m_LoadedDLLPath)) {
            m_Status = GameDLLStatus::Error;
            return false;
        }

        // Get function pointers
        m_Functions.GetVersion = (GameDLL_GetVersion_t)GetSymbol("GameDLL_GetVersion");
        m_Functions.Init = (GameDLL_Init_t)GetSymbol("GameDLL_Init");
        m_Functions.Update = (GameDLL_Update_t)GetSymbol("GameDLL_Update");
        m_Functions.Shutdown = (GameDLL_Shutdown_t)GetSymbol("GameDLL_Shutdown");
        m_Functions.RegisterComponents = (GameDLL_RegisterComponents_t)GetSymbol("GameDLL_RegisterComponents");
        m_Functions.RegisterSystems = (GameDLL_RegisterSystems_t)GetSymbol("GameDLL_RegisterSystems");
        m_Functions.OnBeforeReload = (GameDLL_OnBeforeReload_t)GetSymbol("GameDLL_OnBeforeReload");
        m_Functions.OnAfterReload = (GameDLL_OnAfterReload_t)GetSymbol("GameDLL_OnAfterReload");

        if (!m_Functions.IsValid()) {
            m_LastError = "Game DLL is missing required exports";
            LOG_ERROR(m_LastError);
            FreeLibrary();
            m_Status = GameDLLStatus::Error;
            return false;
        }

        // Check version compatibility
        int dllVersion = m_Functions.GetVersion();
        if (dllVersion != GAME_DLL_INTERFACE_VERSION) {
            m_LastError = "Game DLL version mismatch. Expected: " +
                         std::to_string(GAME_DLL_INTERFACE_VERSION) +
                         ", Got: " + std::to_string(dllVersion);
            LOG_ERROR(m_LastError);
            FreeLibrary();
            m_Status = GameDLLStatus::Error;
            return false;
        }

        m_Status = GameDLLStatus::Loaded;
        m_LastReloadTime = std::chrono::system_clock::now();
        LOG_INFO("Game DLL loaded successfully!");
        return true;
    }

    bool GameDLLLoader::UnloadDLL() {
        if (m_Status != GameDLLStatus::Loaded) {
            return true;
        }

        LOG_INFO("Unloading game DLL...");

        // Call shutdown if available
        if (m_Functions.Shutdown) {
            m_Functions.Shutdown();
        }

        FreeLibrary();
        m_Functions = GameDLLFunctions{}; // Reset function pointers
        m_Status = GameDLLStatus::NotLoaded;

        LOG_INFO("Game DLL unloaded.");
        return true;
    }

    bool GameDLLLoader::ReloadDLL() {
        LOG_INFO("=== Hot-Reload Started ===");
        m_Status = GameDLLStatus::Reloading;

        // Save game state if the DLL supports it
        SaveGameState();

        // Unload current DLL
        if (!UnloadDLL()) {
            m_LastError = "Failed to unload current DLL";
            m_Status = GameDLLStatus::Error;
            if (m_ReloadCallback) m_ReloadCallback(false, m_LastError);
            return false;
        }

        // Small delay to ensure file handles are released
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Load new DLL
        if (!LoadDLL()) {
            if (m_ReloadCallback) m_ReloadCallback(false, m_LastError);
            return false;
        }

        // Restore game state if supported
        RestoreGameState();

        m_ReloadCount++;
        LOG_INFO("=== Hot-Reload Complete (Reload #" + std::to_string(m_ReloadCount) + ") ===");

        if (m_ReloadCallback) m_ReloadCallback(true, "Hot-reload successful!");
        return true;
    }

    bool GameDLLLoader::BuildGameDLL(bool async) {
        if (m_IsBuildInProgress) {
            LOG_WARN("Build already in progress");
            return false;
        }

        if (async) {
            m_IsBuildInProgress = true;
            m_Status = GameDLLStatus::Building;
            std::thread(&GameDLLLoader::BuildThread, this).detach();
            return true;
        } else {
            BuildThread();
            return m_Status != GameDLLStatus::Error;
        }
    }

    void GameDLLLoader::BuildThread() {
        LOG_INFO("Building game DLL...");

        if (m_BuildProgressCallback) m_BuildProgressCallback("Configuring...", 0.1f);

        std::filesystem::path sourcePath = m_ProjectPath / "Source";
        std::filesystem::path buildDir = m_ProjectPath / "TempBuild";

        // Create/clean build directory
        try {
            if (std::filesystem::exists(buildDir)) {
                std::filesystem::remove_all(buildDir);
            }
            std::filesystem::create_directories(buildDir);
        } catch (const std::exception& e) {
            m_LastError = "Failed to create build directory: " + std::string(e.what());
            LOG_ERROR(m_LastError);
            m_IsBuildInProgress = false;
            m_Status = GameDLLStatus::Error;
            return;
        }

        if (m_BuildProgressCallback) m_BuildProgressCallback("Running CMake...", 0.3f);

        // CMake configure command - use BUILD_AS_DLL=ON for the game project template
        std::string configureCmd = "cmake -S \"" + sourcePath.string() + "\" -B \"" + buildDir.string() +
                                   "\" -DBUILD_AS_DLL=ON -DCMAKE_BUILD_TYPE=Debug";

        int result = std::system(configureCmd.c_str());
        if (result != 0) {
            m_LastError = "CMake configure failed";
            LOG_ERROR(m_LastError);
            m_IsBuildInProgress = false;
            m_Status = GameDLLStatus::Error;
            return;
        }

        if (m_BuildProgressCallback) m_BuildProgressCallback("Compiling...", 0.6f);

        // CMake build command - build GameDLL target
        std::string buildCmd = "cmake --build \"" + buildDir.string() + "\" --target GameDLL --config Debug";

        result = std::system(buildCmd.c_str());
        if (result != 0) {
            m_LastError = "Compilation failed";
            LOG_ERROR(m_LastError);
            m_IsBuildInProgress = false;
            m_Status = GameDLLStatus::Error;
            return;
        }

        if (m_BuildProgressCallback) m_BuildProgressCallback("Done!", 1.0f);

        LOG_INFO("Game DLL built successfully!");
        m_IsBuildInProgress = false;

        // If auto-reload is enabled and DLL was previously loaded, reload it
        if (m_AutoReloadEnabled && m_Status == GameDLLStatus::Building) {
            ReloadDLL();
        } else {
            m_Status = GameDLLStatus::NotLoaded;
        }
    }

    void GameDLLLoader::CheckForChanges() {
        if (!m_AutoReloadEnabled || m_IsBuildInProgress) return;

        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastSourceCheck);

        // Check every 500ms
        if (elapsed.count() < 500) return;
        m_LastSourceCheck = now;

        if (CheckSourceModified()) {
            LOG_INFO("Source file changes detected, triggering rebuild...");
            BuildGameDLL(true);
        }
    }

    bool GameDLLLoader::CheckSourceModified() {
        std::filesystem::path sourcePath = m_ProjectPath / "Source";

        if (!std::filesystem::exists(sourcePath)) return false;

        std::chrono::system_clock::time_point latestModify{};

        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                        auto modTime = std::chrono::clock_cast<std::chrono::system_clock>(
                            entry.last_write_time());
                        if (modTime > latestModify) {
                            latestModify = modTime;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Error checking source files: " + std::string(e.what()));
            return false;
        }

        if (latestModify > m_LastSourceModifyTime) {
            m_LastSourceModifyTime = latestModify;
            return m_ReloadCount > 0; // Only trigger if we've loaded at least once
        }

        return false;
    }

    void GameDLLLoader::CallInit(World* world) {
        if (m_Functions.Init && m_Status == GameDLLStatus::Loaded) {
            m_Functions.Init(world);
        }
    }

    void GameDLLLoader::CallUpdate(float deltaTime) {
        if (m_Functions.Update && m_Status == GameDLLStatus::Loaded) {
            m_Functions.Update(deltaTime);
        }
    }

    void GameDLLLoader::CallShutdown() {
        if (m_Functions.Shutdown && m_Status == GameDLLStatus::Loaded) {
            m_Functions.Shutdown();
        }
    }

    void GameDLLLoader::CallRegisterComponents() {
        if (m_Functions.RegisterComponents && m_Status == GameDLLStatus::Loaded) {
            m_Functions.RegisterComponents();
        }
    }

    void GameDLLLoader::CallRegisterSystems(World* world) {
        if (m_Functions.RegisterSystems && m_Status == GameDLLStatus::Loaded) {
            m_Functions.RegisterSystems(world);
        }
    }

    std::string GameDLLLoader::GetStatusString() const {
        switch (m_Status) {
            case GameDLLStatus::NotLoaded: return "Not Loaded";
            case GameDLLStatus::Loading: return "Loading...";
            case GameDLLStatus::Loaded: return "Loaded";
            case GameDLLStatus::Building: return "Building...";
            case GameDLLStatus::Reloading: return "Reloading...";
            case GameDLLStatus::Error: return "Error";
            default: return "Unknown";
        }
    }

    // Platform-specific implementations

    #if defined(_WIN32) || defined(_WIN64)

    bool GameDLLLoader::LoadLibrary(const std::filesystem::path& path) {
        m_Handle = ::LoadLibraryW(path.wstring().c_str());
        if (!m_Handle) {
            DWORD error = ::GetLastError();
            m_LastError = "LoadLibrary failed with error code: " + std::to_string(error);
            LOG_ERROR(m_LastError);
            return false;
        }
        return true;
    }

    void GameDLLLoader::FreeLibrary() {
        if (m_Handle) {
            ::FreeLibrary(m_Handle);
            m_Handle = nullptr;
        }
    }

    void* GameDLLLoader::GetSymbol(const char* name) {
        if (!m_Handle) return nullptr;
        return (void*)::GetProcAddress(m_Handle, name);
    }

    bool GameDLLLoader::CopyDLLForLoading() {
        // Generate unique temp filename
        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        m_LoadedDLLPath = m_ProjectPath / "Builds" / ("GameDLL_" + std::to_string(timestamp) + ".dll");

        try {
            std::filesystem::copy_file(m_DLLPath, m_LoadedDLLPath,
                std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            m_LastError = "Failed to copy DLL: " + std::string(e.what());
            LOG_ERROR(m_LastError);
            return false;
        }

        return true;
    }

    void GameDLLLoader::CleanupTempDLLs() {
        try {
            std::filesystem::path buildsDir = m_ProjectPath / "Builds";
            for (const auto& entry : std::filesystem::directory_iterator(buildsDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (filename.starts_with("GameDLL_") && filename.ends_with(".dll")) {
                        // Don't delete the currently loaded one
                        if (entry.path() != m_LoadedDLLPath) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore cleanup errors
        }
    }

    #else // Linux/macOS

    bool GameDLLLoader::LoadLibrary(const std::filesystem::path& path) {
        m_Handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_Handle) {
            m_LastError = "dlopen failed: " + std::string(dlerror());
            LOG_ERROR(m_LastError);
            return false;
        }
        return true;
    }

    void GameDLLLoader::FreeLibrary() {
        if (m_Handle) {
            dlclose(m_Handle);
            m_Handle = nullptr;
        }
    }

    void* GameDLLLoader::GetSymbol(const char* name) {
        if (!m_Handle) return nullptr;
        return dlsym(m_Handle, name);
    }

    bool GameDLLLoader::CopyDLLForLoading() {
        // On Unix, we can often overwrite loaded libraries, but copy anyway for safety
        auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        #if defined(__APPLE__)
            m_LoadedDLLPath = m_ProjectPath / "Builds" / ("libGameDLL_" + std::to_string(timestamp) + ".dylib");
        #else
            m_LoadedDLLPath = m_ProjectPath / "Builds" / ("libGameDLL_" + std::to_string(timestamp) + ".so");
        #endif

        try {
            std::filesystem::copy_file(m_DLLPath, m_LoadedDLLPath,
                std::filesystem::copy_options::overwrite_existing);
        } catch (const std::exception& e) {
            m_LastError = "Failed to copy DLL: " + std::string(e.what());
            LOG_ERROR(m_LastError);
            return false;
        }

        return true;
    }

    void GameDLLLoader::CleanupTempDLLs() {
        try {
            std::filesystem::path buildsDir = m_ProjectPath / "Builds";
            for (const auto& entry : std::filesystem::directory_iterator(buildsDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    #if defined(__APPLE__)
                        if (filename.starts_with("libGameDLL_") && filename.ends_with(".dylib")) {
                    #else
                        if (filename.starts_with("libGameDLL_") && filename.ends_with(".so")) {
                    #endif
                        if (entry.path() != m_LoadedDLLPath) {
                            std::filesystem::remove(entry.path());
                        }
                    }
                }
            }
        } catch (...) {
            // Ignore cleanup errors
        }
    }

    #endif

    void GameDLLLoader::SaveGameState() {
        if (m_Functions.OnBeforeReload && m_Status == GameDLLStatus::Loaded) {
            LOG_INFO("Saving game state before reload...");
            if (m_SavedState) {
                free(m_SavedState);
                m_SavedState = nullptr;
            }
            m_SavedState = m_Functions.OnBeforeReload(&m_SavedStateSize);
        }
    }

    void GameDLLLoader::RestoreGameState() {
        if (m_Functions.OnAfterReload && m_SavedState && m_SavedStateSize > 0) {
            LOG_INFO("Restoring game state after reload...");
            m_Functions.OnAfterReload(m_SavedState, m_SavedStateSize);
            free(m_SavedState);
            m_SavedState = nullptr;
            m_SavedStateSize = 0;
        }
    }

} // namespace Editor
