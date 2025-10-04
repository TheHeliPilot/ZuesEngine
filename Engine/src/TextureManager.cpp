//
// Created by bucka on 10/3/2025.
//

#include "../include/Engine/TextureManager.h"
#include "../include/Engine/TextureManager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>

#include "../include/Engine/EngineDefines.h" // Assuming this contains LOG_... macros
#include <glad/glad.h>                      // Required for OpenGL calls
#include "../include//stb/stb_image.h"               // Required for image loading

namespace Engine {

    // Initialize the static map storage
    std::unordered_map<std::string, TextureInfo> TextureManager::s_TextureMap;

    // Add these functions to your TextureManager class

// Helper function to get filename without extension
std::string TextureManager::GetFileNameWithoutExtension(const std::string& filePath) {
    std::filesystem::path p(filePath);
    return p.stem().string();
}

// Modified LoadTexture - now uses filename without extension as default key
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

    const std::vector<std::filesystem::path> metafilePaths = FindWildcardMetaFile(filePath, ".spriteMeta");
    const uint32_t CURRENT_META_FILE_VERSION = 1;

    // Get filename without extension for default sprite name
    std::string defaultSpriteName = GetFileNameWithoutExtension(filePath);

    if (metafilePaths.empty()) {
        // Create default metafile
        std::string metaFileName = filePath + ".spriteMeta";

        std::ofstream file(metaFileName);
        if (!file.is_open()) {
            LOG_ERROR("Unable to save metafile for texture: " + filePath);
            glDeleteTextures(1, &textureID);
            return;
        }

        file << "SpriteMetaFileVersion: " << CURRENT_META_FILE_VERSION << "\n";
        file << "SpriteName: " << defaultSpriteName << "\n";
        file << "Texture UV Rect:\n";
        file << "0\n0\n1\n1\n";
        file.close();

        // Store in map with filename (no extension) as key
        TextureInfo info;
        info.ID = textureID;
        info.Name = defaultSpriteName;
        info.TextureUVRect = {0.0f, 0.0f, 1.0f, 1.0f};
        info.Width = width;
        info.Height = height;
        info.SourceFilePath = filePath;
        s_TextureMap[defaultSpriteName] = info;

        LOG_INFO("Created default sprite: '" + defaultSpriteName + "' from " + filePath);
    } else {
        // Load from existing metafiles
        for (const auto& metafilePath : metafilePaths) {
            std::ifstream file(metafilePath);
            if (!file.is_open()) {
                LOG_ERROR("Unable to open metafile: " + metafilePath.string());
                continue;
            }

            TextureInfo info;
            std::string line;
            uint32_t version = 0;
            bool expectingUVRectValue = false;
            int uvRectValuesRead = 0;

            while (std::getline(file, line)) {
                if (line.empty()) continue;

                if (line.find("SpriteMetaFileVersion: ") == 0) {
                    try {
                        version = std::stoul(line.substr(23));
                        if (version != CURRENT_META_FILE_VERSION) {
                            LOG_WARN("Metafile version mismatch in " + metafilePath.string());
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to parse version in " + metafilePath.string());
                        continue;
                    }
                }
                else if (line.find("SpriteName: ") == 0) {
                    info.Name = line.substr(12);
                }
                else if (line.find("Texture UV Rect:") == 0) {
                    expectingUVRectValue = true;
                    uvRectValuesRead = 0;
                }
                else if (expectingUVRectValue && uvRectValuesRead < 4) {
                    try {
                        float value = std::stof(line);
                        if (uvRectValuesRead == 0) info.TextureUVRect.x = value;
                        else if (uvRectValuesRead == 1) info.TextureUVRect.y = value;
                        else if (uvRectValuesRead == 2) info.TextureUVRect.z = value;
                        else if (uvRectValuesRead == 3) info.TextureUVRect.w = value;

                        uvRectValuesRead++;
                        if (uvRectValuesRead == 4) {
                            expectingUVRectValue = false;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to parse UV value in " + metafilePath.string());
                    }
                }
            }

            file.close();

            if (version == 0 || uvRectValuesRead < 4) {
                LOG_ERROR("Invalid metafile format: " + metafilePath.string());
                continue;
            }

            // Store texture info
            info.ID = textureID;
            info.Width = width;
            info.Height = height;
            info.SourceFilePath = filePath;
            s_TextureMap[info.Name] = info;
        }
    }

    LOG_INFO("Texture loaded successfully: " + filePath + " (ID: " + std::to_string(textureID) + ")");
}

// NEW FUNCTION: Create a new sprite from an existing texture
bool TextureManager::CreateSpriteFromTexture(
    const std::string& sourceTexturePath,
    const std::string& newSpriteName,
    const Math::Vec4& uvRect  // x, y, width, height in [0,1] range
) {
    // 1. Validate UV coordinates
    if (uvRect.x < 0.0f || uvRect.y < 0.0f ||
        uvRect.z < 0.0f || uvRect.w < 0.0f ||
        uvRect.x + uvRect.z > 1.0f || uvRect.y + uvRect.w > 1.0f) {
        LOG_ERROR("Invalid UV coordinates for sprite: " + newSpriteName);
        return false;
    }

    // 2. Find the source texture in our map to get its texture ID
    uint32_t sourceTextureID = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;

    // Search through all loaded textures to find one with matching source path
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

    // 3. Check if sprite name already exists
    if (s_TextureMap.count(newSpriteName) > 0) {
        LOG_ERROR("Sprite name already exists: " + newSpriteName);
        return false;
    }

    // 4. Get the next available metafile number
    uint32_t nextNumber = GetNextMetaFileNumber(sourceTexturePath, ".spriteMeta");

    // 5. Create the metafile path
    std::string metaFileName = sourceTexturePath + "." + std::to_string(nextNumber) + ".spriteMeta";

    // 6. Write the metafile
    std::ofstream file(metaFileName);
    if (!file.is_open()) {
        LOG_ERROR("Unable to create metafile: " + metaFileName);
        return false;
    }

    const uint32_t CURRENT_META_FILE_VERSION = 2;
    file << "SpriteMetaFileVersion: " << CURRENT_META_FILE_VERSION << "\n";
    file << "SpriteName: " << newSpriteName << "\n";
    file << "SourceFilePath: " << sourceTexturePath << "\n";  // ADD THIS LINE
    file << "Texture UV Rect:\n";
    file << uvRect.x << "\n";
    file << uvRect.y << "\n";
    file << uvRect.z << "\n";
    file << uvRect.w << "\n";
    file.close();

    // 7. Add the new sprite to the texture map
    TextureInfo info;
    info.ID = sourceTextureID;  // Share the same OpenGL texture ID
    info.Name = newSpriteName;
    info.TextureUVRect = uvRect;
    info.Width = sourceWidth;
    info.Height = sourceHeight;
    info.SourceFilePath = sourceTexturePath;

    s_TextureMap[newSpriteName] = info;

    LOG_INFO("Created new sprite '" + newSpriteName + "' from " + sourceTexturePath + " (metafile: " + metaFileName + ")");
    return true;
}

TextureInfo TextureManager::GetTexture(const std::string& textureName) {
    auto it = s_TextureMap.find(textureName);
    if (it == s_TextureMap.end()) {
        if(textureName != "")
            LOG_WARN("Texture not found: " + textureName);
        return TextureInfo{};
    }

    // If texture ID is 0, it hasn't been loaded yet - lazy load it now
    if (it->second.ID == 0 && !it->second.SourceFilePath.empty()) {
        LOG_INFO("Lazy-loading texture: " + textureName);

        // Load the actual OpenGL texture
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

        LOG_INFO("Lazy-loaded texture ID " + std::to_string(textureID) + " for: " + textureName);
    }

    return it->second;
}

    void TextureManager::Shutdown() {
        LOG_INFO("Shutting down TextureManager. Deleting " + std::to_string(s_TextureMap.size()) + " textures.");

        for (const auto& pair : s_TextureMap) {
            glDeleteTextures(1, &pair.second.ID);
        }
        s_TextureMap.clear();
    }

    namespace fs = std::filesystem;

    std::vector<fs::path> Engine::TextureManager::FindWildcardMetaFile(const std::string &fullFilePath,
                                                                       const std::string &requiredSuffix) {
        std::vector<fs::path> foundMetaFiles;

        const fs::path baseAssetPath(fullFilePath);
        const fs::path parentDir = baseAssetPath.parent_path();
        const std::string baseName = baseAssetPath.filename().string();
        const size_t baseLen = baseName.length();
        const size_t suffixLen = requiredSuffix.length();

        // 0. Pre-check: Ensure the base directory exists
        if (!fs::exists(parentDir) || !fs::is_directory(parentDir)) {
            return foundMetaFiles;
        }

        // Iterate through all entries in the directory
        for (const auto& entry : fs::directory_iterator(parentDir)) {

            // 1. Skip non-file entries
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string fileName = entry.path().filename().string();
            const size_t fileNameLen = fileName.length();

            // 2. Preliminary pattern checks: Must start with baseName and end with requiredSuffix
            if (!fileName.starts_with(baseName) || !fileName.ends_with(requiredSuffix)) {
                continue;
            }

            // 3. Minimum length check: baseName + dot + digit + dot + suffix
            // e.g., if baseName="tex.png" (7), suffix=".meta" (5), min length is 7+1+1+1+5 = 15
            if (fileNameLen < baseLen + suffixLen + 2) {
                continue;
            }

            // 4. Extract and validate the intermediate part (e.g., ".1." from "tex.png.1.meta")
            // Intermediate starts immediately after baseName and ends immediately before requiredSuffix.
            const size_t intermediateLen = fileNameLen - baseLen - suffixLen;
            const std::string intermediate = fileName.substr(baseLen, intermediateLen);

            // Check for the required dot delimiters:
            // Must start with '.' and end with '.'
            if (intermediate.empty() || intermediate[0] != '.' || intermediate.back() != '.') {
                continue;
            }

            // 5. Extract the numerical part (e.g., "1" from ".1.")
            // Substring starts after the initial dot (index 1) and removes the final dot (length - 2)
            const std::string numberSegment = intermediate.substr(1, intermediate.length() - 2);

            // 6. Final validation: Check if the segment is non-empty and contains ONLY digits
            if (!numberSegment.empty() &&
                std::ranges::all_of(numberSegment,
                                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            {
                // If all checks pass, add the full path to the results vector
                foundMetaFiles.push_back(entry.path());
            }
        }

        return foundMetaFiles;
    }

    uint32_t TextureManager::GetNextMetaFileNumber(const std::string& fullFilePath, const std::string &requiredSuffix) {
        uint32_t maxNumber = 0;

        const fs::path baseAssetPath(fullFilePath);
        const fs::path parentDir = baseAssetPath.parent_path();
        const std::string baseName = baseAssetPath.filename().string();
        const size_t baseLen = baseName.length();
        const size_t suffixLen = requiredSuffix.length();

        // 0. Pre-check: If the directory doesn't exist, the first available number is 1
        if (!fs::exists(parentDir) || !fs::is_directory(parentDir)) {
            return 1;
        }

        // Iterate through all entries in the directory
        for (const auto& entry : fs::directory_iterator(parentDir)) {

            // 1. Skip non-file entries
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string fileName = entry.path().filename().string();
            const size_t fileNameLen = fileName.length();

            // 2. Preliminary pattern checks
            if (!fileName.starts_with(baseName) || !fileName.ends_with(requiredSuffix)) {
                continue;
            }

            // 3. Minimum length check
            if (fileNameLen < baseLen + suffixLen + 2) {
                continue;
            }

            // 4. Extract and validate the intermediate part
            const size_t intermediateLen = fileNameLen - baseLen - suffixLen;
            const std::string intermediate = fileName.substr(baseLen, intermediateLen);

            // Check for the required dot delimiters
            if (intermediate.empty() || intermediate[0] != '.' || intermediate.back() != '.') {
                continue;
            }

            // 5. Extract the numerical part
            const std::string numberSegment = intermediate.substr(1, intermediate.length() - 2);

            // 6. Final validation and extraction: Check if the segment is non-empty and contains ONLY digits
            if (!numberSegment.empty() &&
                std::ranges::all_of(numberSegment,
                                    [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            {
                // Convert the segment to a number
                try {
                    // Use stringstream for robust conversion of the numerical segment
                    uint32_t currentNumber;
                    std::stringstream ss(numberSegment);
                    if (ss >> currentNumber) {
                        // Update the maximum found number
                        if (currentNumber > maxNumber) {
                            maxNumber = currentNumber;
                        }
                    }
                } catch (...) {
                    // If parsing fails for any reason (e.g., number too large), just ignore the file
                }
            }
        }

        // The next available number is the maximum found number plus one.
        // If maxNumber is 0 (no matching files found), this returns 1.
        return maxNumber + 1;
    }

    std::unordered_map<std::string, TextureInfo> &TextureManager::GetAllTextures() {
        return TextureManager::s_TextureMap;
    }

    void TextureManager::RemoveSprite(const std::string& spriteName) {
        // Delete the metafile and remove from map
        TextureManager::s_TextureMap.erase(spriteName);
        // TODO: Also delete the physical .spriteMeta file
    }

    void TextureManager::ScanAndRegisterAllSprites(const std::string& projectRootPath) {
        LOG_INFO("Scanning project for sprite metadata: " + projectRootPath);

        namespace fs = std::filesystem;
        const fs::path projectRoot(projectRootPath);

        if (!fs::exists(projectRoot) || !fs::is_directory(projectRoot)) {
            LOG_ERROR("Project root does not exist: " + projectRootPath);
            return;
        }

        int metafilesFound = 0;
        int metafilesLoaded = 0;

        // Recursively iterate through all files in project
        for (const auto& entry : fs::recursive_directory_iterator(projectRoot)) {
            if (!entry.is_regular_file()) continue;

            const std::string filename = entry.path().filename().string();

            // Check if it's a .spriteMeta file
            if (!filename.ends_with(".spriteMeta")) continue;

            metafilesFound++;

            // Try to find the associated texture
            fs::path texturePath = FindTextureForMetafile(entry.path(), projectRootPath);

            if (texturePath.empty()) {
                LOG_WARN("Could not find texture for metafile: " + entry.path().string());
                continue;
            }

            // Parse the metafile
            TextureInfo info = ParseMetafileWithoutTexture(entry.path(), texturePath);

            if (info.Name.empty()) {
                LOG_ERROR("Failed to parse metafile: " + entry.path().string());
                continue;
            }

            // Register in map (texture will be lazy-loaded when needed)
            s_TextureMap[info.Name] = info;
            metafilesLoaded++;

            LOG_INFO("Registered sprite: " + info.Name + " from " + entry.path().string());
        }

        LOG_INFO("Sprite scan complete: Found " + std::to_string(metafilesFound) +
                 " metafiles, loaded " + std::to_string(metafilesLoaded) + " sprites");
    }

    // Finds the texture file associated with a metafile using multiple strategies
    std::filesystem::path TextureManager::FindTextureForMetafile(
        const std::filesystem::path& metafilePath,
        const std::string& projectRoot) {

        namespace fs = std::filesystem;

        // Strategy 1: Read SourceFilePath from metafile and check if it exists
        std::string storedPath = ReadSourcePathFromMetafile(metafilePath);
        if (!storedPath.empty() && fs::exists(storedPath)) {
            return fs::path(storedPath);
        }

        // Strategy 2: Look for texture in the same directory as metafile
        // Extract base texture name from metafile name
        // e.g., "player.png.1.spriteMeta" -> "player.png"
        std::string metafileName = metafilePath.filename().string();
        std::string baseName = ExtractTextureNameFromMetafile(metafileName);

        if (!baseName.empty()) {
            fs::path sameDir = metafilePath.parent_path() / baseName;
            if (fs::exists(sameDir)) {
                // Update the metafile with corrected path
                UpdateSourcePathInMetafile(metafilePath, sameDir.string());
                return sameDir;
            }
        }

        // Strategy 3: Search entire project for matching texture filename
        if (!baseName.empty()) {
            for (const auto& entry : fs::recursive_directory_iterator(projectRoot)) {
                if (entry.is_regular_file() && entry.path().filename().string() == baseName) {
                    // Found it! Update metafile
                    UpdateSourcePathInMetafile(metafilePath, entry.path().string());
                    return entry.path();
                }
            }
        }

        return {}; // Not found
    }

    // Parse metafile without loading the actual OpenGL texture
    TextureInfo TextureManager::ParseMetafileWithoutTexture(
        const std::filesystem::path& metafilePath,
        const std::filesystem::path& texturePath) {

        TextureInfo info;
        info.ID = 0; // Not loaded yet - will be lazy-loaded
        info.SourceFilePath = texturePath.string();

        std::ifstream file(metafilePath);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open metafile: " + metafilePath.string());
            return info;
        }

        const uint32_t CURRENT_VERSION = 1;
        std::string line;
        uint32_t version = 0;
        bool expectingUVRectValue = false;
        int uvRectValuesRead = 0;

        while (std::getline(file, line)) {
            if (line.empty()) continue;

            if (line.starts_with("SpriteMetaFileVersion: ")) {
                try {
                    version = std::stoul(line.substr(23));
                } catch (...) {
                    LOG_ERROR("Invalid version in metafile");
                    return TextureInfo{};
                }
            }
            else if (line.starts_with("SpriteName: ")) {
                info.Name = line.substr(12);
            }
            else if (line.find("Texture UV Rect:") == 0) {
                expectingUVRectValue = true;
                uvRectValuesRead = 0;
            }
            else if (expectingUVRectValue && uvRectValuesRead < 4) {
                try {
                    float value = std::stof(line);
                    if (uvRectValuesRead == 0) info.TextureUVRect.x = value;
                    else if (uvRectValuesRead == 1) info.TextureUVRect.y = value;
                    else if (uvRectValuesRead == 2) info.TextureUVRect.z = value;
                    else if (uvRectValuesRead == 3) info.TextureUVRect.w = value;

                    uvRectValuesRead++;
                    if (uvRectValuesRead == 4) {
                        expectingUVRectValue = false;
                    }
                } catch (...) {
                    LOG_ERROR("Invalid UV value in metafile");
                }
            }
        }

        file.close();

        if (version == 0 || uvRectValuesRead < 4 || info.Name.empty()) {
            LOG_ERROR("Incomplete metafile: " + metafilePath.string());
            return TextureInfo{};
        }

        // Get image dimensions without loading full texture (use stbi_info)
        int width, height, channels;
        if (stbi_info(texturePath.string().c_str(), &width, &height, &channels)) {
            info.Width = width;
            info.Height = height;
        }

        return info;
    }

    // Helper: Extract texture filename from metafile name
    std::string TextureManager::ExtractTextureNameFromMetafile(const std::string& metafileName) {
        // Examples:
        // "player.png.spriteMeta" -> "player.png"
        // "player.png.1.spriteMeta" -> "player.png"
        // "texture.jpg.42.spriteMeta" -> "texture.jpg"

        const std::string suffix = ".spriteMeta";
        if (!metafileName.ends_with(suffix)) {
            return "";
        }

        // Remove .spriteMeta
        std::string temp = metafileName.substr(0, metafileName.length() - suffix.length());

        // Check if there's a number before .spriteMeta (e.g., ".1")
        size_t lastDot = temp.find_last_of('.');
        if (lastDot != std::string::npos) {
            std::string afterDot = temp.substr(lastDot + 1);

            // Check if it's all digits
            if (std::all_of(afterDot.begin(), afterDot.end(), ::isdigit)) {
                // Remove the number part
                temp = temp.substr(0, lastDot);
            }
        }

        return temp;
    }

    // Helper: Read SourceFilePath from metafile
    std::string TextureManager::ReadSourcePathFromMetafile(const std::filesystem::path& metafilePath) {
        std::ifstream file(metafilePath);
        if (!file.is_open()) return "";

        std::string line;
        while (std::getline(file, line)) {
            if (line.starts_with("SourceFilePath: ")) {
                return line.substr(16);
            }
        }

        return "";
    }

    // Helper: Update SourceFilePath in metafile
    void TextureManager::UpdateSourcePathInMetafile(
        const std::filesystem::path& metafilePath,
        const std::string& newPath) {

        // Read entire file
        std::ifstream inFile(metafilePath);
        if (!inFile.is_open()) {
            LOG_ERROR("Cannot open metafile for update: " + metafilePath.string());
            return;
        }

        std::vector<std::string> lines;
        std::string line;
        bool foundSourcePath = false;

        while (std::getline(inFile, line)) {
            if (line.starts_with("SourceFilePath: ")) {
                lines.push_back("SourceFilePath: " + newPath);
                foundSourcePath = true;
            } else {
                lines.push_back(line);
            }
        }
        inFile.close();

        // If SourceFilePath wasn't in the file, add it after SpriteName
        if (!foundSourcePath) {
            auto it = std::find_if(lines.begin(), lines.end(),
                [](const std::string& l) { return l.starts_with("SpriteName: "); });

            if (it != lines.end()) {
                lines.insert(it + 1, "SourceFilePath: " + newPath);
            }
        }

        // Write back
        std::ofstream outFile(metafilePath);
        if (!outFile.is_open()) {
            LOG_ERROR("Cannot write metafile: " + metafilePath.string());
            return;
        }

        for (const auto& l : lines) {
            outFile << l << "\n";
        }

        outFile.close();
        LOG_INFO("Updated metafile path: " + metafilePath.string());
    }

    std::string TextureManager::GetSpriteNameByID(uint32_t textureID) {
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

        // Sort alphabetically for better UX
        std::sort(names.begin(), names.end());

        return names;
    }
}
