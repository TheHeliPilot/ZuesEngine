#ifndef ZUESENGINE_TEXTUREMANAGER_H
#define ZUESENGINE_TEXTUREMANAGER_H

#include "ZuesAPI.h"
#include "SpriteMetaFile.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "ZuesMath.h"

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

        static TextureInfo GetTextureInfo(const std::string& textureName);

        static void Shutdown();

        static std::unordered_map<std::string, TextureInfo> &GetAllTextures();

        static void RemoveSprite(const std::string &spriteName);

        // Scans project directory for all .sprite.meta files and registers sprites
        static void ScanAndRegisterAllSprites(const std::string& projectRootPath);

        // Get sprite name from texture ID (returns empty string if not found)
        static std::string GetSpriteNameByID(uint32_t textureID);

        // Get all sprite names (useful for dropdown)
        static std::vector<std::string> GetAllSpriteNames();

        static bool UpdateSpriteUVRect(const std::string &spriteName, const Math::Vec4 &newUVRect);

        // Get the SpriteMetaFile for a given texture path (loads/creates if needed)
        static SpriteMetaFile* GetOrCreateMetaFile(const std::filesystem::path& texturePath);

        // Get all sprites from a specific texture
        static std::vector<std::string> GetSpritesFromTexture(const std::filesystem::path& texturePath);

        // Reload sprites from a meta file (after editing in texture cutter)
        static void ReloadMetaFile(const std::filesystem::path& texturePath);

        // Static map to store the loaded texture information, keyed by file path.
        static std::unordered_map<std::string, TextureInfo> s_TextureMap;

        // Map of texture paths to their meta files
        static std::unordered_map<std::string, SpriteMetaFile> s_MetaFiles;
    };

}

#endif // ZUESENGINE_TEXTUREMANAGER_H
