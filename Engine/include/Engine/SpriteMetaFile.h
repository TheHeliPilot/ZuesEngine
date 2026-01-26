#pragma once

#include "ZuesAPI.h"
#include "ZuesMath.h"
#include <string>
#include <vector>
#include <filesystem>

namespace Engine {

    /**
     * @brief Represents a single sprite region within a texture.
     * UV coordinates are in OpenGL convention (0,0 at bottom-left).
     */
    struct ZUES_API SpriteRegion {
        std::string name;
        Math::Vec4 uvRect = {0.0f, 0.0f, 1.0f, 1.0f}; // x, y, width, height in [0,1]

        // Pivot point in normalized coordinates (0,0 = bottom-left, 1,1 = top-right)
        // Default is center (0.5, 0.5)
        Math::Vec2 pivot = {0.5f, 0.5f};

        // Pixel coordinates (for editor display)
        int pixelX = 0;
        int pixelY = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
    };

    /**
     * @brief JSON-based sprite metadata file handler.
     *
     * File format (.sprite.meta):
     * {
     *     "version": 1,
     *     "sourceTexture": "path/to/texture.png",
     *     "textureWidth": 512,
     *     "textureHeight": 512,
     *     "sprites": [
     *         {
     *             "name": "default",
     *             "uvRect": { "x": 0.0, "y": 0.0, "width": 1.0, "height": 1.0 },
     *             "pixelRect": { "x": 0, "y": 0, "width": 512, "height": 512 }
     *         }
     *     ]
     * }
     */
    class ZUES_API SpriteMetaFile {
    public:
        static constexpr int CURRENT_VERSION = 1;

        // Meta file data
        int version = CURRENT_VERSION;
        std::filesystem::path sourceTexturePath;
        int textureWidth = 0;
        int textureHeight = 0;
        std::vector<SpriteRegion> sprites;

        // The path to this meta file
        std::filesystem::path metaFilePath;

        /**
         * @brief Load a .sprite.meta file from disk.
         * @param metaPath Path to the .sprite.meta file
         * @return True if loaded successfully
         */
        bool Load(const std::filesystem::path& metaPath);

        /**
         * @brief Save this meta file to disk.
         * @return True if saved successfully
         */
        bool Save() const;

        /**
         * @brief Save to a specific path.
         * @param metaPath Path to save to
         * @return True if saved successfully
         */
        bool SaveTo(const std::filesystem::path& metaPath) const;

        /**
         * @brief Create a default sprite meta file for a texture.
         * @param texturePath Path to the source texture
         * @param width Texture width in pixels
         * @param height Texture height in pixels
         * @return The created SpriteMetaFile
         */
        static SpriteMetaFile CreateDefault(
            const std::filesystem::path& texturePath,
            int width, int height);

        /**
         * @brief Get the expected meta file path for a texture.
         * @param texturePath Path to the texture file
         * @return Path to the corresponding .sprite.meta file
         */
        static std::filesystem::path GetMetaFilePath(const std::filesystem::path& texturePath);

        /**
         * @brief Check if a meta file exists for a texture.
         * @param texturePath Path to the texture file
         * @return True if .sprite.meta exists
         */
        static bool MetaFileExists(const std::filesystem::path& texturePath);

        /**
         * @brief Fix the source texture path if the texture has been moved.
         * Searches in the same directory as the meta file and parent directories.
         * @return True if the path was fixed
         */
        bool FixSourceTexturePath();

        /**
         * @brief Add a new sprite region.
         * @param name Sprite name (must be unique within this file)
         * @param uvRect UV coordinates (OpenGL convention)
         * @return True if added successfully
         */
        bool AddSprite(const std::string& name, const Math::Vec4& uvRect);

        /**
         * @brief Update an existing sprite region.
         * @param name Sprite name to update
         * @param uvRect New UV coordinates
         * @return True if updated successfully
         */
        bool UpdateSprite(const std::string& name, const Math::Vec4& uvRect);

        /**
         * @brief Remove a sprite region.
         * @param name Sprite name to remove
         * @return True if removed successfully
         */
        bool RemoveSprite(const std::string& name);

        /**
         * @brief Find a sprite by name.
         * @param name Sprite name
         * @return Pointer to sprite, or nullptr if not found
         */
        SpriteRegion* FindSprite(const std::string& name);
        const SpriteRegion* FindSprite(const std::string& name) const;

        /**
         * @brief Rename a sprite.
         * @param oldName Current sprite name
         * @param newName New sprite name
         * @return True if renamed successfully
         */
        bool RenameSprite(const std::string& oldName, const std::string& newName);

        /**
         * @brief Convert pixel coordinates to UV rect.
         * @param pixelX X position in pixels (from top-left)
         * @param pixelY Y position in pixels (from top-left)
         * @param pixelWidth Width in pixels
         * @param pixelHeight Height in pixels
         * @return UV rect in OpenGL convention (bottom-left origin)
         */
        Math::Vec4 PixelToUV(int pixelX, int pixelY, int pixelWidth, int pixelHeight) const;

        /**
         * @brief Convert UV rect to pixel coordinates.
         * @param uvRect UV coordinates
         * @param outX Output X position in pixels
         * @param outY Output Y position in pixels (from top-left)
         * @param outWidth Output width in pixels
         * @param outHeight Output height in pixels
         */
        void UVToPixel(const Math::Vec4& uvRect, int& outX, int& outY, int& outWidth, int& outHeight) const;
    };

} // namespace Engine
