#include "../include/Engine/SpriteMetaFile.h"
#include "../include/Engine/EngineDefines.h"
#include "../include/json/json.hpp"

#include <fstream>

namespace Engine {

    using json = nlohmann::json;

    bool SpriteMetaFile::Load(const std::filesystem::path& metaPath) {
        std::ifstream file(metaPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open sprite meta file: " + metaPath.string());
            return false;
        }

        try {
            json j;
            file >> j;
            file.close();

            version = j.value("version", 1);
            sourceTexturePath = j.value("sourceTexture", "");
            textureWidth = j.value("textureWidth", 0);
            textureHeight = j.value("textureHeight", 0);
            metaFilePath = metaPath;

            sprites.clear();
            if (j.contains("sprites") && j["sprites"].is_array()) {
                for (const auto& spriteJson : j["sprites"]) {
                    SpriteRegion region;
                    region.name = spriteJson.value("name", "unnamed");

                    if (spriteJson.contains("uvRect")) {
                        const auto& uv = spriteJson["uvRect"];
                        region.uvRect.x = uv.value("x", 0.0f);
                        region.uvRect.y = uv.value("y", 0.0f);
                        region.uvRect.z = uv.value("width", 1.0f);
                        region.uvRect.w = uv.value("height", 1.0f);
                    }

                    if (spriteJson.contains("pivot")) {
                        const auto& pv = spriteJson["pivot"];
                        region.pivot.x = pv.value("x", 0.5f);
                        region.pivot.y = pv.value("y", 0.5f);
                    }

                    if (spriteJson.contains("pixelRect")) {
                        const auto& px = spriteJson["pixelRect"];
                        region.pixelX = px.value("x", 0);
                        region.pixelY = px.value("y", 0);
                        region.pixelWidth = px.value("width", textureWidth);
                        region.pixelHeight = px.value("height", textureHeight);
                    } else {
                        // Calculate pixel rect from UV
                        UVToPixel(region.uvRect, region.pixelX, region.pixelY,
                                  region.pixelWidth, region.pixelHeight);
                    }

                    sprites.push_back(region);
                }
            }

            LOG_INFO("Loaded sprite meta: " + metaPath.string() + " with " +
                     std::to_string(sprites.size()) + " sprites");

            // Try to fix texture path if it doesn't exist
            FixSourceTexturePath();

            return true;

        } catch (const std::exception& e) {
            LOG_ERROR("Failed to parse sprite meta file: " + metaPath.string() +
                      " - " + e.what());
            return false;
        }
    }

    bool SpriteMetaFile::Save() const {
        if (metaFilePath.empty()) {
            LOG_ERROR("Cannot save sprite meta: no file path set");
            return false;
        }
        return SaveTo(metaFilePath);
    }

    bool SpriteMetaFile::SaveTo(const std::filesystem::path& metaPath) const {
        json j;
        j["version"] = version;
        j["sourceTexture"] = sourceTexturePath.generic_string();
        j["textureWidth"] = textureWidth;
        j["textureHeight"] = textureHeight;

        j["sprites"] = json::array();
        for (const auto& sprite : sprites) {
            json spriteJson;
            spriteJson["name"] = sprite.name;
            spriteJson["uvRect"] = {
                {"x", sprite.uvRect.x},
                {"y", sprite.uvRect.y},
                {"width", sprite.uvRect.z},
                {"height", sprite.uvRect.w}
            };
            spriteJson["pivot"] = {
                {"x", sprite.pivot.x},
                {"y", sprite.pivot.y}
            };
            spriteJson["pixelRect"] = {
                {"x", sprite.pixelX},
                {"y", sprite.pixelY},
                {"width", sprite.pixelWidth},
                {"height", sprite.pixelHeight}
            };
            j["sprites"].push_back(spriteJson);
        }

        std::ofstream file(metaPath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to write sprite meta file: " + metaPath.string());
            return false;
        }

        file << j.dump(4); // Pretty print with 4-space indent
        file.close();

        LOG_INFO("Saved sprite meta: " + metaPath.string());
        return true;
    }

    SpriteMetaFile SpriteMetaFile::CreateDefault(
        const std::filesystem::path& texturePath,
        int width, int height) {

        SpriteMetaFile meta;
        meta.version = CURRENT_VERSION;
        meta.sourceTexturePath = texturePath;
        meta.textureWidth = width;
        meta.textureHeight = height;
        meta.metaFilePath = GetMetaFilePath(texturePath);

        // Create default sprite covering entire texture
        SpriteRegion defaultSprite;
        defaultSprite.name = texturePath.stem().string(); // filename without extension
        defaultSprite.uvRect = {0.0f, 0.0f, 1.0f, 1.0f};
        defaultSprite.pixelX = 0;
        defaultSprite.pixelY = 0;
        defaultSprite.pixelWidth = width;
        defaultSprite.pixelHeight = height;

        meta.sprites.push_back(defaultSprite);

        return meta;
    }

    std::filesystem::path SpriteMetaFile::GetMetaFilePath(const std::filesystem::path& texturePath) {
        // texture.png -> texture.png.sprite.meta
        return std::filesystem::path(texturePath.string() + ".sprite.meta");
    }

    bool SpriteMetaFile::MetaFileExists(const std::filesystem::path& texturePath) {
        return std::filesystem::exists(GetMetaFilePath(texturePath));
    }

    bool SpriteMetaFile::FixSourceTexturePath() {
        namespace fs = std::filesystem;

        // If the current path is valid, nothing to fix
        if (fs::exists(sourceTexturePath)) {
            return true;
        }

        std::string textureFilename = sourceTexturePath.filename().string();
        LOG_WARN("Texture not found at: " + sourceTexturePath.string() + ", searching...");

        // Strategy 1: Check same directory as meta file
        if (!metaFilePath.empty()) {
            fs::path sameDir = metaFilePath.parent_path() / textureFilename;
            if (fs::exists(sameDir)) {
                sourceTexturePath = sameDir;
                LOG_INFO("Found texture in same directory: " + sameDir.string());
                Save(); // Update meta file
                return true;
            }
        }

        // Strategy 2: Check if meta file name suggests the texture location
        // e.g., "texture.png.sprite.meta" means texture is "texture.png" in same dir
        if (!metaFilePath.empty()) {
            std::string metaName = metaFilePath.filename().string();
            const std::string suffix = ".sprite.meta";
            if (metaName.ends_with(suffix)) {
                std::string possibleTextureName = metaName.substr(0, metaName.length() - suffix.length());
                fs::path possiblePath = metaFilePath.parent_path() / possibleTextureName;
                if (fs::exists(possiblePath)) {
                    sourceTexturePath = possiblePath;
                    LOG_INFO("Found texture from meta name: " + possiblePath.string());
                    Save();
                    return true;
                }
            }
        }

        // Strategy 3: Search parent directories (up to 3 levels)
        if (!metaFilePath.empty()) {
            fs::path searchDir = metaFilePath.parent_path();
            for (int i = 0; i < 3 && !searchDir.empty(); ++i) {
                fs::path candidate = searchDir / textureFilename;
                if (fs::exists(candidate)) {
                    sourceTexturePath = candidate;
                    LOG_INFO("Found texture in parent: " + candidate.string());
                    Save();
                    return true;
                }
                searchDir = searchDir.parent_path();
            }
        }

        // Strategy 4: Search subdirectories of meta file location
        if (!metaFilePath.empty()) {
            try {
                for (const auto& entry : fs::recursive_directory_iterator(metaFilePath.parent_path())) {
                    if (entry.is_regular_file() && entry.path().filename().string() == textureFilename) {
                        sourceTexturePath = entry.path();
                        LOG_INFO("Found texture in subdirectory: " + entry.path().string());
                        Save();
                        return true;
                    }
                }
            } catch (...) {
                // Ignore errors during recursive search
            }
        }

        LOG_ERROR("Could not find texture file: " + textureFilename);
        return false;
    }

    bool SpriteMetaFile::AddSprite(const std::string& name, const Math::Vec4& uvRect) {
        // Check if name already exists
        if (FindSprite(name) != nullptr) {
            LOG_ERROR("Sprite already exists: " + name);
            return false;
        }

        SpriteRegion region;
        region.name = name;
        region.uvRect = uvRect;
        UVToPixel(uvRect, region.pixelX, region.pixelY, region.pixelWidth, region.pixelHeight);

        sprites.push_back(region);
        return true;
    }

    bool SpriteMetaFile::UpdateSprite(const std::string& name, const Math::Vec4& uvRect) {
        SpriteRegion* sprite = FindSprite(name);
        if (!sprite) {
            LOG_ERROR("Sprite not found: " + name);
            return false;
        }

        sprite->uvRect = uvRect;
        UVToPixel(uvRect, sprite->pixelX, sprite->pixelY, sprite->pixelWidth, sprite->pixelHeight);
        return true;
    }

    bool SpriteMetaFile::RemoveSprite(const std::string& name) {
        auto it = std::find_if(sprites.begin(), sprites.end(),
            [&name](const SpriteRegion& s) { return s.name == name; });

        if (it == sprites.end()) {
            LOG_ERROR("Sprite not found: " + name);
            return false;
        }

        sprites.erase(it);
        return true;
    }

    SpriteRegion* SpriteMetaFile::FindSprite(const std::string& name) {
        for (auto& sprite : sprites) {
            if (sprite.name == name) {
                return &sprite;
            }
        }
        return nullptr;
    }

    const SpriteRegion* SpriteMetaFile::FindSprite(const std::string& name) const {
        for (const auto& sprite : sprites) {
            if (sprite.name == name) {
                return &sprite;
            }
        }
        return nullptr;
    }

    bool SpriteMetaFile::RenameSprite(const std::string& oldName, const std::string& newName) {
        // Check new name doesn't exist
        if (FindSprite(newName) != nullptr) {
            LOG_ERROR("Sprite name already exists: " + newName);
            return false;
        }

        SpriteRegion* sprite = FindSprite(oldName);
        if (!sprite) {
            LOG_ERROR("Sprite not found: " + oldName);
            return false;
        }

        sprite->name = newName;
        return true;
    }

    Math::Vec4 SpriteMetaFile::PixelToUV(int pixelX, int pixelY, int pixelWidth, int pixelHeight) const {
        if (textureWidth <= 0 || textureHeight <= 0) {
            return {0.0f, 0.0f, 1.0f, 1.0f};
        }

        // Convert from top-left origin (editor) to bottom-left origin (OpenGL)
        float uvX = static_cast<float>(pixelX) / textureWidth;
        float uvWidth = static_cast<float>(pixelWidth) / textureWidth;
        float uvHeight = static_cast<float>(pixelHeight) / textureHeight;

        // Y flip: top-left to bottom-left
        float topY = static_cast<float>(pixelY) / textureHeight;
        float uvY = 1.0f - topY - uvHeight;

        return {uvX, uvY, uvWidth, uvHeight};
    }

    void SpriteMetaFile::UVToPixel(const Math::Vec4& uvRect, int& outX, int& outY,
                                    int& outWidth, int& outHeight) const {
        outX = static_cast<int>(uvRect.x * textureWidth);
        outWidth = static_cast<int>(uvRect.z * textureWidth);
        outHeight = static_cast<int>(uvRect.w * textureHeight);

        // Y flip: bottom-left (OpenGL) to top-left (editor)
        float bottomY = uvRect.y;
        float topY = 1.0f - bottomY - uvRect.w;
        outY = static_cast<int>(topY * textureHeight);
    }

} // namespace Engine
