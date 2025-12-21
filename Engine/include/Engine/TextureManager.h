#ifndef ZUESENGINE_TEXTUREMANAGER_H
#define ZUESENGINE_TEXTUREMANAGER_H

#include "ZuesAPI.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <bits/stl_vector.h>

#include "Math.h" // Assuming this is needed for Engine:: namespace or similar context

namespace Engine {

    /**
     * @brief Structure holding basic metadata for a loaded OpenGL texture.
     */
    struct ZUES_API TextureInfo {
        uint32_t ID = 0;
        int Width = 0;
        int Height = 0;
        std::string Name = "DEFAULT";
        std::filesystem::path SourceFilePath = "";
        Math::Vec4 TextureUVRect = { 0.0f, 0.0f, 1.0f, 1.0f };
    };

    /**
     * @brief Manages the loading, storage, and retrieval of texture assets.
     * This class uses a static approach for easy access across the engine.
     */
    class ZUES_API TextureManager {
    public:
        static TextureInfo GetTexture(const std::string &textureName);

        static std::string GetFileNameWithoutExtension(const std::string &filePath);

        static void LoadTexture(const std::string &filePath);

        static bool CreateSpriteFromTexture(const std::string &sourceTexturePath, const std::string &newSpriteName,
                                            const Math::Vec4 &uvRect);

        static TextureInfo ParseMetaFile(const std::filesystem::path &metafilePath, uint32_t expectedVersion);

        static TextureInfo GetTextureInfo(const std::string& textureName);

        static void Shutdown();

        static std::vector<std::filesystem::path> FindWildcardMetaFile(const std::string &fullFilePath,
                                                                       const std::string &requiredSuffix);

        static uint32_t GetNextMetaFileNumber(const std::string &fullFilePath, const std::string &requiredSuffix);

        static std::unordered_map<std::string, TextureInfo> &GetAllTextures();

        static void RemoveSprite(const std::string &spriteName);

        // Scans project directory for all .spriteMeta files and registers sprites
        static void ScanAndRegisterAllSprites(const std::string& projectRootPath);

        // Finds the texture file for a given metafile using multiple strategies
        static std::filesystem::path FindTextureForMetafile(
            const std::filesystem::path& metafilePath,
            const std::string& projectRoot);

        // Parse metafile without loading OpenGL texture (for startup scanning)
        static TextureInfo ParseMetafileWithoutTexture(
            const std::filesystem::path& metafilePath,
            const std::filesystem::path& texturePath);

        // Helper: Extract base texture filename from metafile name
        static std::string ExtractTextureNameFromMetafile(const std::string& metafileName);

        // Helper: Read SourceFilePath from metafile
        static std::string ReadSourcePathFromMetafile(const std::filesystem::path& metafilePath);

        // Get sprite name from texture ID (returns empty string if not found)
        static std::string GetSpriteNameByID(uint32_t textureID);

        // Get all sprite names (useful for dropdown)
        static std::vector<std::string> GetAllSpriteNames();

        static bool UpdateSpriteUVRect(const std::string &spriteName, const Math::Vec4 &newUVRect);

        // Helper: Update SourceFilePath in metafile
        static void UpdateSourcePathInMetafile(
            const std::filesystem::path& metafilePath,
            const std::string& newPath);

        // Static map to store the loaded texture information, keyed by file path.
        static std::unordered_map<std::string, TextureInfo> s_TextureMap;
    };

}

#endif // ZUESENGINE_TEXTUREMANAGER_H
