//
// Created by bucka on 10/3/2025.
//

#include "../include/Engine/TextureManager.h"
#include "../include/Engine/ZuesAPI.h"
#include "../include/Engine/SpriteMetaFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <set>

#include "../include/Engine/EngineDefines.h"
#include <glad/glad.h>
#include "../include//stb/stb_image.h"

namespace Engine {

    namespace fs = std::filesystem;

    // Initialize the static map storage - MUST be exported for game DLLs
    ZUES_API std::unordered_map<std::string, TextureInfo> TextureManager::s_TextureMap;
    ZUES_API std::unordered_map<std::string, SpriteMetaFile> TextureManager::s_MetaFiles;

    std::string TextureManager::GetFileNameWithoutExtension(const std::string& filePath) {
        fs::path p(filePath);
        return p.stem().string();
    }

    void TextureManager::LoadTexture(const std::string &filePath) {
        // Load image data using stb_image
        int width, height, channels;
        stbi_set_flip_vertically_on_load(1);
        unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

        if (!data) {
            LOG_ERROR("Failed to load texture file: " + filePath);
            return;
        }

        // Determine OpenGL format based on channels
        GLenum internalFormat = 0, dataFormat = 0;
        if (channels == 4) {
            internalFormat = GL_RGBA8;
            dataFormat = GL_RGBA;
        } else if (channels == 3) {
            internalFormat = GL_RGB8;
            dataFormat = GL_RGB;
        } else {
            LOG_ERROR("Unsupported texture channel count (" + std::to_string(channels) + ") for: " + filePath);
            stbi_image_free(data);
            return;
        }

        // Create OpenGL texture object
        uint32_t textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // Load the data into the texture
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Unbind the texture and free the local image buffer
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);

        // Get or create meta file
        fs::path texturePath(filePath);
        SpriteMetaFile* meta = GetOrCreateMetaFile(texturePath);

        if (!meta) {
            LOG_ERROR("Failed to get/create meta file for: " + filePath);
            glDeleteTextures(1, &textureID);
            return;
        }

        // Register all sprites from the meta file
        for (const auto& sprite : meta->sprites) {
            TextureInfo info;
            info.ID = textureID;
            info.Name = sprite.name;
            info.TextureUVRect = sprite.uvRect;
            info.Width = width;
            info.Height = height;
            info.SourceFilePath = filePath;
            s_TextureMap[sprite.name] = info;

            LOG_INFO("Registered sprite: '" + sprite.name + "' from " + filePath);
        }

        LOG_INFO("Texture loaded successfully: " + filePath + " (ID: " + std::to_string(textureID) + ")");
    }

    SpriteMetaFile* TextureManager::GetOrCreateMetaFile(const std::filesystem::path& texturePath) {
        std::string key = texturePath.generic_string();

        // Check if already loaded
        auto it = s_MetaFiles.find(key);
        if (it != s_MetaFiles.end()) {
            return &it->second;
        }

        fs::path metaPath = SpriteMetaFile::GetMetaFilePath(texturePath);

        SpriteMetaFile meta;
        if (fs::exists(metaPath)) {
            // Load existing meta file
            if (!meta.Load(metaPath)) {
                LOG_ERROR("Failed to load meta file: " + metaPath.string());
                return nullptr;
            }
        } else {
            // Create new meta file - need to get texture dimensions
            int width, height, channels;
            if (!stbi_info(texturePath.string().c_str(), &width, &height, &channels)) {
                LOG_ERROR("Failed to get texture info for: " + texturePath.string());
                return nullptr;
            }

            meta = SpriteMetaFile::CreateDefault(texturePath, width, height);
            if (!meta.Save()) {
                LOG_ERROR("Failed to save new meta file: " + metaPath.string());
                return nullptr;
            }
            LOG_INFO("Created new sprite meta: " + metaPath.string());
        }

        s_MetaFiles[key] = std::move(meta);
        return &s_MetaFiles[key];
    }

    bool TextureManager::CreateSpriteFromTexture(
        const std::string& sourceTexturePath,
        const std::string& newSpriteName,
        const Math::Vec4& uvRect
    ) {
        // Validate UV coordinates
        if (uvRect.x < 0.0f || uvRect.y < 0.0f ||
            uvRect.z < 0.0f || uvRect.w < 0.0f ||
            uvRect.x + uvRect.z > 1.0f || uvRect.y + uvRect.w > 1.0f) {
            LOG_ERROR("Invalid UV coordinates for sprite: " + newSpriteName);
            return false;
        }

        // Check if sprite name already exists
        if (s_TextureMap.contains(newSpriteName)) {
            LOG_ERROR("Sprite name already exists: " + newSpriteName);
            return false;
        }

        // Find the source texture info
        uint32_t sourceTextureID = 0;
        int sourceWidth = 0;
        int sourceHeight = 0;
        bool foundSource = false;

        for (const auto& [key, texInfo] : s_TextureMap) {
            if (texInfo.SourceFilePath == sourceTexturePath) {
                sourceTextureID = texInfo.ID;
                sourceWidth = texInfo.Width;
                sourceHeight = texInfo.Height;
                foundSource = true;
                break;
            }
        }

        if (!foundSource) {
            LOG_ERROR("Source texture not loaded: " + sourceTexturePath);
            return false;
        }

        // Get/create meta file and add sprite
        SpriteMetaFile* meta = GetOrCreateMetaFile(fs::path(sourceTexturePath));
        if (!meta) {
            LOG_ERROR("Failed to get meta file for: " + sourceTexturePath);
            return false;
        }

        if (!meta->AddSprite(newSpriteName, uvRect)) {
            return false;
        }

        if (!meta->Save()) {
            LOG_ERROR("Failed to save meta file after adding sprite");
            return false;
        }

        // Add to texture map
        TextureInfo info;
        info.ID = sourceTextureID;
        info.Name = newSpriteName;
        info.TextureUVRect = uvRect;
        info.Width = sourceWidth;
        info.Height = sourceHeight;
        info.SourceFilePath = sourceTexturePath;

        s_TextureMap[newSpriteName] = info;

        LOG_INFO("Created new sprite '" + newSpriteName + "' from " + sourceTexturePath);
        return true;
    }

    TextureInfo TextureManager::GetTexture(const std::string& textureName) {
        auto it = s_TextureMap.find(textureName);
        if (it == s_TextureMap.end()) {
            if (!textureName.empty())
                LOG_WARN("Texture not found: " + textureName);
            return TextureInfo{};
        }

        // If texture ID is 0, it hasn't been loaded yet - lazy load it now
        if (it->second.ID == 0 && !it->second.SourceFilePath.empty()) {
            LOG_INFO("Lazy-loading texture: " + textureName);

            int width, height, channels;
            stbi_set_flip_vertically_on_load(1);
            unsigned char* data = stbi_load(it->second.SourceFilePath.string().c_str(),
                                           &width, &height, &channels, 0);

            if (!data) {
                LOG_ERROR("Failed to lazy-load texture: " + it->second.SourceFilePath.string());
                return it->second;
            }

            GLenum internalFormat = 0, dataFormat = 0;
            if (channels == 4) {
                internalFormat = GL_RGBA8;
                dataFormat = GL_RGBA;
            } else if (channels == 3) {
                internalFormat = GL_RGB8;
                dataFormat = GL_RGB;
            } else {
                LOG_ERROR("Unsupported channel count for lazy-load: " + std::to_string(channels));
                stbi_image_free(data);
                return it->second;
            }

            uint32_t textureID;
            glGenTextures(1, &textureID);
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                        dataFormat, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, 0);
            stbi_image_free(data);

            // Update the map entry with the loaded texture ID
            it->second.ID = textureID;
            it->second.Width = width;
            it->second.Height = height;

            // Also update any other sprites sharing this texture
            std::string sourcePath = it->second.SourceFilePath.string();
            for (auto& [name, info] : s_TextureMap) {
                if (info.SourceFilePath == sourcePath && info.ID == 0) {
                    info.ID = textureID;
                    info.Width = width;
                    info.Height = height;
                }
            }

            LOG_INFO("Lazy-loaded texture ID " + std::to_string(textureID) + " for: " + textureName);
        }

        return it->second;
    }

    void TextureManager::Shutdown() {
        LOG_INFO("Shutting down TextureManager. Deleting " + std::to_string(s_TextureMap.size()) + " textures.");

        // Track deleted texture IDs to avoid double deletion
        std::set<uint32_t> deletedIDs;

        for (const auto& pair : s_TextureMap) {
            if (pair.second.ID != 0 && !deletedIDs.contains(pair.second.ID)) {
                glDeleteTextures(1, &pair.second.ID);
                deletedIDs.insert(pair.second.ID);
            }
        }
        s_TextureMap.clear();
        s_MetaFiles.clear();
    }

    std::unordered_map<std::string, TextureInfo> &TextureManager::GetAllTextures() {
        return TextureManager::s_TextureMap;
    }

    void TextureManager::RemoveSprite(const std::string& spriteName) {
        auto it = s_TextureMap.find(spriteName);
        if (it == s_TextureMap.end()) {
            LOG_WARN("Cannot remove sprite - not found: " + spriteName);
            return;
        }

        // Find and update the meta file
        std::string sourcePath = it->second.SourceFilePath.generic_string();
        auto metaIt = s_MetaFiles.find(sourcePath);
        if (metaIt != s_MetaFiles.end()) {
            metaIt->second.RemoveSprite(spriteName);
            metaIt->second.Save();
        }

        s_TextureMap.erase(spriteName);
        LOG_INFO("Removed sprite: " + spriteName);
    }

    void TextureManager::ScanAndRegisterAllSprites(const std::string& projectRootPath) {
        LOG_INFO("Scanning project for sprite metadata: " + projectRootPath);

        const fs::path projectRoot(projectRootPath);

        if (!fs::exists(projectRoot) || !fs::is_directory(projectRoot)) {
            LOG_ERROR("Project root does not exist: " + projectRootPath);
            return;
        }

        int metafilesFound = 0;
        int spritesLoaded = 0;

        // Recursively iterate through all files in project
        for (const auto& entry : fs::recursive_directory_iterator(projectRoot)) {
            if (!entry.is_regular_file()) continue;

            const std::string filename = entry.path().filename().string();

            // Check if it's a .sprite.meta file
            if (!filename.ends_with(".sprite.meta")) continue;

            metafilesFound++;

            SpriteMetaFile meta;
            if (!meta.Load(entry.path())) {
                LOG_WARN("Failed to load meta file: " + entry.path().string());
                continue;
            }

            // Verify texture exists
            if (!fs::exists(meta.sourceTexturePath)) {
                // Try to find texture relative to meta file
                fs::path textureInSameDir = entry.path().parent_path() / meta.sourceTexturePath.filename();
                if (fs::exists(textureInSameDir)) {
                    meta.sourceTexturePath = textureInSameDir;
                    meta.Save(); // Update the meta file with corrected path
                } else {
                    LOG_WARN("Texture not found for meta file: " + entry.path().string());
                    continue;
                }
            }

            // Store meta file
            std::string metaKey = meta.sourceTexturePath.generic_string();
            s_MetaFiles[metaKey] = meta;

            // Register all sprites (lazy load - ID = 0)
            for (const auto& sprite : meta.sprites) {
                TextureInfo info;
                info.ID = 0; // Will be lazy-loaded
                info.Name = sprite.name;
                info.TextureUVRect = sprite.uvRect;
                info.Width = meta.textureWidth;
                info.Height = meta.textureHeight;
                info.SourceFilePath = meta.sourceTexturePath;

                s_TextureMap[sprite.name] = info;
                spritesLoaded++;
            }
        }

        LOG_INFO("Sprite scan complete: Found " + std::to_string(metafilesFound) +
                 " meta files, loaded " + std::to_string(spritesLoaded) + " sprites");
    }

    std::string TextureManager::GetSpriteNameByID(const uint32_t textureID) {
        for (const auto& [name, info] : s_TextureMap) {
            if (info.ID == textureID) {
                return name;
            }
        }
        return "";
    }

    std::vector<std::string> TextureManager::GetAllSpriteNames() {
        std::vector<std::string> names;
        names.reserve(s_TextureMap.size());

        for (const auto& [name, info] : s_TextureMap) {
            names.push_back(name);
        }

        std::ranges::sort(names);
        return names;
    }

    std::vector<std::string> TextureManager::GetSpritesFromTexture(const std::filesystem::path& texturePath) {
        std::vector<std::string> spriteNames;

        auto it = s_MetaFiles.find(texturePath.generic_string());
        if (it != s_MetaFiles.end()) {
            for (const auto& sprite : it->second.sprites) {
                spriteNames.push_back(sprite.name);
            }
        }

        return spriteNames;
    }

    void TextureManager::ReloadMetaFile(const std::filesystem::path& texturePath) {
        std::string key = texturePath.generic_string();

        // Find existing texture ID (if loaded)
        uint32_t existingID = 0;
        for (const auto& [name, info] : s_TextureMap) {
            if (info.SourceFilePath == texturePath) {
                existingID = info.ID;
                break;
            }
        }

        // Remove old sprites from this texture
        std::vector<std::string> toRemove;
        for (const auto& [name, info] : s_TextureMap) {
            if (info.SourceFilePath == texturePath) {
                toRemove.push_back(name);
            }
        }
        for (const auto& name : toRemove) {
            s_TextureMap.erase(name);
        }

        // Remove old meta file
        s_MetaFiles.erase(key);

        // Reload
        fs::path metaPath = SpriteMetaFile::GetMetaFilePath(texturePath);
        if (!fs::exists(metaPath)) {
            LOG_WARN("Meta file does not exist: " + metaPath.string());
            return;
        }

        SpriteMetaFile meta;
        if (!meta.Load(metaPath)) {
            LOG_ERROR("Failed to reload meta file: " + metaPath.string());
            return;
        }

        s_MetaFiles[key] = meta;

        // Re-register sprites
        for (const auto& sprite : meta.sprites) {
            TextureInfo info;
            info.ID = existingID; // Reuse existing texture ID
            info.Name = sprite.name;
            info.TextureUVRect = sprite.uvRect;
            info.Width = meta.textureWidth;
            info.Height = meta.textureHeight;
            info.SourceFilePath = texturePath;

            s_TextureMap[sprite.name] = info;
        }

        LOG_INFO("Reloaded meta file: " + metaPath.string() + " with " +
                 std::to_string(meta.sprites.size()) + " sprites");
    }

    bool TextureManager::UpdateSpriteUVRect(const std::string& spriteName, const Math::Vec4& newUVRect) {
        auto it = s_TextureMap.find(spriteName);
        if (it == s_TextureMap.end()) {
            LOG_ERROR("Sprite not found: " + spriteName);
            return false;
        }

        // Validate UV coordinates
        if (newUVRect.x < 0.0f || newUVRect.y < 0.0f ||
            newUVRect.z < 0.0f || newUVRect.w < 0.0f ||
            newUVRect.x + newUVRect.z > 1.0f || newUVRect.y + newUVRect.w > 1.0f) {
            LOG_ERROR("Invalid UV coordinates for sprite: " + spriteName);
            return false;
        }

        // Update in-memory sprite info
        it->second.TextureUVRect = newUVRect;

        // Find and update meta file
        std::string sourcePath = it->second.SourceFilePath.generic_string();
        auto metaIt = s_MetaFiles.find(sourcePath);
        if (metaIt != s_MetaFiles.end()) {
            if (metaIt->second.UpdateSprite(spriteName, newUVRect)) {
                metaIt->second.Save();
                LOG_INFO("Updated UV rect for sprite: " + spriteName);
                return true;
            }
        }

        LOG_ERROR("Failed to update meta file for sprite: " + spriteName);
        return false;
    }

    TextureInfo TextureManager::GetTextureInfo(const std::string& textureName) {
        return GetTexture(textureName);
    }

} // namespace Engine
