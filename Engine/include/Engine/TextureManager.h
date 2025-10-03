#ifndef ZUESENGINE_TEXTUREMANAGER_H
#define ZUESENGINE_TEXTUREMANAGER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include "Math.h" // Assuming this is needed for Engine:: namespace or similar context

namespace Engine {

    /**
     * @brief Structure holding basic metadata for a loaded OpenGL texture.
     */
    struct TextureInfo {
        uint32_t ID = 0;
        int Width = 0;
        int Height = 0;
    };

    /**
     * @brief Manages the loading, storage, and retrieval of texture assets.
     * This class uses a static approach for easy access across the engine.
     */
    class TextureManager {
    public:
        /**
         * @brief Loads a texture from a file path, creates an OpenGL texture object,
         * and stores its ID and metadata under the given file path as the key.
         * @param filePath The path to the texture file (e.g., PNG, JPG).
         * @return The OpenGL texture ID, or 0 if loading fails.
         */
        static uint32_t LoadTexture(const std::string& filePath);

        /**
         * @brief Retrieves the OpenGL ID of a previously loaded texture.
         * @param filePath The file path used during the LoadTexture call.
         * @return The OpenGL texture ID, or 0 if not found.
         */
        static uint32_t GetTextureID(const std::string& filePath);

        /**
         * @brief Retrieves the metadata (ID, Width, Height) of a loaded texture.
         * @param filePath The file path used during the LoadTexture call.
         * @return A TextureInfo struct, or one with ID=0 if not found.
         */
        static TextureInfo GetTextureInfo(const std::string& filePath);

        /**
         * @brief Cleans up all loaded OpenGL texture resources and clears the map.
         */
        static void Shutdown();

    private:
        // Static map to store the loaded texture information, keyed by file path.
        static std::unordered_map<std::string, TextureInfo> s_TextureMap;
    };

}

#endif // ZUESENGINE_TEXTUREMANAGER_H
