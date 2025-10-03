//
// Created by bucka on 10/3/2025.
//

#include "../include/Engine/TextureManager.h"
#include "../include/Engine/TextureManager.h"
#include "../include/Engine/EngineDefines.h" // Assuming this contains LOG_... macros
#include <glad/glad.h>                      // Required for OpenGL calls
#include "../include//stb/stb_image.h"               // Required for image loading

namespace Engine {

    // Initialize the static map storage
    std::unordered_map<std::string, TextureInfo> TextureManager::s_TextureMap;

    uint32_t TextureManager::LoadTexture(const std::string& filePath) {
        // 1. Check if texture is already loaded
        if (s_TextureMap.count(filePath)) {
            LOG_WARN("Texture already loaded: " + filePath);
            return s_TextureMap[filePath].ID;
        }

        // 2. Load image data using stb_image
        int width, height, channels;
        // Flip images vertically to match OpenGL's coordinate system
        stbi_set_flip_vertically_on_load(1);
        unsigned char* data = stbi_load(filePath.c_str(), &width, &height, &channels, 0);

        if (!data) {
            LOG_ERROR("Failed to load texture file: " + filePath);
            return 0;
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
            // Handle other channel counts if necessary
            LOG_ERROR("Unsupported texture channel count (" + std::to_string(channels) + ") for: " + filePath);
            stbi_image_free(data);
            return 0;
        }

        // 3. Create OpenGL texture object
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

        // 4. Store the texture info in the map
        s_TextureMap[filePath] = {textureID, width, height};
        LOG_INFO("Texture loaded successfully: " + filePath + " (ID: " + std::to_string(textureID) + ")");

        return textureID;
    }

    uint32_t TextureManager::GetTextureID(const std::string& filePath) {
        if (s_TextureMap.count(filePath)) {
            return s_TextureMap[filePath].ID;
        }
        return 0;
    }

    TextureInfo TextureManager::GetTextureInfo(const std::string& filePath) {
        if (s_TextureMap.count(filePath)) {
            return s_TextureMap[filePath];
        }
        return {0, 0, 0}; // Return empty info if not found
    }

    void TextureManager::Shutdown() {
        LOG_INFO("Shutting down TextureManager. Deleting " + std::to_string(s_TextureMap.size()) + " textures.");

        for (const auto& pair : s_TextureMap) {
            glDeleteTextures(1, &pair.second.ID);
        }
        s_TextureMap.clear();
    }
}
