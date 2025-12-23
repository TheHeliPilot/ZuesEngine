// GameDLLLoader.h - Handles loading, unloading, and hot-reloading of game DLLs
#pragma once

#include "Engine.h"
#include "GameDLLInterface.h"
#include <string>
#include <filesystem>
#include <functional>
#include <chrono>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    using DLLHandle = HMODULE;
#else
    #include <dlfcn.h>
    using DLLHandle = void*;
#endif

namespace Editor {

    // Callback types for UI notifications
    using ReloadCallback = std::function<void(bool success, const std::string& message)>;
    using BuildProgressCallback = std::function<void(const std::string& stage, float progress)>;

    // Status of the game DLL
    enum class GameDLLStatus {
        NotLoaded,
        Loading,
        Loaded,
        Building,
        Reloading,
        Error
    };

    class GameDLLLoader {
    public:
        static GameDLLLoader& Get();

        // Initialize the loader with project path
        bool Initialize(const std::filesystem::path& projectPath);
        void Shutdown();

        // DLL lifecycle
        bool LoadDLL();
        bool UnloadDLL();
        bool ReloadDLL();

        // Build the game DLL
        bool BuildGameDLL(bool async = true);
        bool IsBuildInProgress() const { return m_IsBuildInProgress; }

        // Hot-reload support
        void EnableAutoReload(bool enable) { m_AutoReloadEnabled = enable; }
        bool IsAutoReloadEnabled() const { return m_AutoReloadEnabled; }
        void CheckForChanges(); // Call every frame to check for source changes

        // Game DLL interface
        void CallInit(World* world, std::map<Engine::ECS::Component::TypeID, Engine::ECS::Component::ComponentCreator> *compReg) const;
        void CallUpdate(float deltaTime) const;
        void CallShutdown() const;
        void CallRegisterComponents() const;
        void CallRegisterSystems(World* world) const;

        // Status and info
        GameDLLStatus GetStatus() const { return m_Status; }
        std::string GetStatusString() const;
        const std::string& GetLastError() const { return m_LastError; }
        bool IsLoaded() const { return m_Status == GameDLLStatus::Loaded; }

        // Info for UI
        std::filesystem::path GetDLLPath() const { return m_DLLPath; }
        std::filesystem::path GetProjectPath() const { return m_ProjectPath; }
        std::chrono::system_clock::time_point GetLastReloadTime() const { return m_LastReloadTime; }
        int GetReloadCount() const { return m_ReloadCount; }

        // Callbacks
        void SetReloadCallback(ReloadCallback callback) { m_ReloadCallback = std::move(callback); }
        void SetBuildProgressCallback(BuildProgressCallback callback) { m_BuildProgressCallback = std::move(callback); }

    private:
        GameDLLLoader() = default;
        ~GameDLLLoader();

        // Platform-specific loading
        bool LoadLibrary(const std::filesystem::path& path);
        void FreeLibrary();
        void* GetSymbol(const char* name) const;

        // Hot-reload helpers
        bool CopyDLLForLoading();  // Copy DLL to temp location (Windows can't overwrite loaded DLLs)
        void CleanupTempDLLs() const;
        bool CheckSourceModified();
        void SaveGameState();
        void RestoreGameState();

        // Build helpers
        void BuildThread();

        // State
        DLLHandle m_Handle = nullptr;
        GameDLLFunctions m_Functions;
        GameDLLStatus m_Status = GameDLLStatus::NotLoaded;
        std::string m_LastError;

        // Paths
        std::filesystem::path m_ProjectPath;
        std::filesystem::path m_DLLPath;        // Original DLL location
        std::filesystem::path m_LoadedDLLPath;  // Temp copy that's actually loaded

        // Hot-reload state
        bool m_AutoReloadEnabled = false;
        bool m_IsBuildInProgress = false;
        std::chrono::system_clock::time_point m_LastSourceCheck;
        std::chrono::system_clock::time_point m_LastSourceModifyTime;
        std::chrono::system_clock::time_point m_LastReloadTime;
        int m_ReloadCount = 0;

        // State preservation for hot-reload
        void* m_SavedState = nullptr;
        size_t m_SavedStateSize = 0;

        // Callbacks
        ReloadCallback m_ReloadCallback;
        BuildProgressCallback m_BuildProgressCallback;
    };

} // namespace Editor
