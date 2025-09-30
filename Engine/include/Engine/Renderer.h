#ifndef ZUESENGINE_RENDERER_H
#define ZUESENGINE_RENDERER_H

#include <cstdint> // For uint32_t
#include <array>   // For std::array
#include <string>
#include <unordered_map>
#include <vector>

#include "Math.h"


namespace Engine {

    struct Line {
        Math::Vec2 start;
        Math::Vec2 end;
        float thickness;
    };

    struct Rect {
        Math::Vec2 position;
        Math::Vec2 size;
        float rotationRadians;
    };

    struct Circle {
        Math::Vec2 center;
        float radius;
        bool outlineOnly = false;
        float thickness = 1.0f; // only used if outlineOnly = true
    };

    struct Arrow {
        Math::Vec2 start;
        Math::Vec2 end;
        float thickness;
    };

    struct Triangle {
        Math::Vec2 v0;
        Math::Vec2 v1;
        Math::Vec2 v2;
    };

    // --- Renderer Class ---

    class Renderer {
    public:
        // --- Core Lifecycle ---
        static void Init();
        static void Render();
        static void Shutdown();

        // --- 2D Batching Pipeline ---

        static void BeginBatch();
        static void EndBatch();

        // Submits a single quad to the current batch.
        static void SubmitQuad(
            const Math::Vec2& position,
            float rotationRadians, // Rotation in radians (fixed parameter name)
            const Math::Vec2& size,
            const Math::Vec4& color,
            uint32_t textureID // OpenGL texture handle (0 for pure color/white)
        );

        static void SubmitQuad(const Math::Vec2 &position, float rotation, const Math::Vec2 &size, const Math::Vec4 &color, uint32_t textureID,
                               float z);

        // --- Camera Management ---

        // Fix: Added the rotationRadians parameter to the declaration.
        // Updates the camera's view-projection matrix uniform in the shader.
        // halfHeight defines the size of the view volume (e.g., 10 world units vertically).
        static void SetCamera(const Math::Vec2& position, float zoom, float halfHeight, float rotationRadians);

        static void SetTextureUniforms();

        // --- Editor Interface ---
        static uint32_t GetRenderTextureID();
        static void SetViewportSize(float width, float height);
        static uint32_t LoadTexture(const std::string& filePath);
        static void SetClearColor(const Math::Vec4& color);

        static Line DrawLine(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float thickness = 1.0f);
        static Rect DrawRect(const Math::Vec2& position, const Math::Vec2& size, const Math::Vec4& color, float rotationRadians = 0.0f);
        static Circle DrawCircle(const Math::Vec2& center, float radius, const Math::Vec4& color, int segments = 20, bool outlineOnly = false, float thickness = 1.0f);
        static Arrow DrawArrow(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float thickness = 1.0f);

        static Math::Vec2 WorldToScreen(const Math::Vec2 &worldPos);
        static Math::Vec2 ScreenToWorld(const Math::Vec2 &screenPos);

        // --- Font System ---
        struct Glyph {
            Math::Vec2 Size;      // Pixel size of glyph
            Math::Vec2 Bearing;   // Offset from baseline to left/top of glyph
            float Advance;  // Advance to next glyph
            Math::Vec2 UV0;       // Bottom-left texcoord
            Math::Vec2 UV1;       // Top-right texcoord
        };

        struct Font {
            uint32_t TextureID;                   // OpenGL texture for the atlas
            float LineHeight;                     // Font size in pixels
            std::unordered_map<char, Glyph> Glyphs;
        };

        static uint32_t LoadFont(const std::string& path, float pixelHeight);
        static void DrawText(const std::string& text,
                             const Math::Vec2& position,
                             float scale,
                             const Math::Vec4& color,
                             uint32_t fontID);

    private:

        static std::vector<Font> s_Fonts;

        // 2D Vertex Structure for Batching
        struct Vertex {
            Math::Vec3 Position; // Engine::Math::Vec3
            Math::Vec4 Color;    // Engine::Math::Vec4
            Math::Vec2 TexCoord; // Engine::Math::Vec2
            float TexID;   // Texture slot index (0.0 to 31.0)
        };

        // Constants for Batching Limits
        static constexpr uint32_t MAX_TEXTURE_SLOTS = 32;

        // Internal struct to hold the graphics context and FBO data
        struct RendererData {
            // --- Framebuffer Data ---
            uint32_t EditorFBO = 0;
            uint32_t ColorAttachment = 0;
            uint32_t DepthAttachment = 0;

            // Camera/Viewport Data
            float ViewportWidth = 0.0f;
            float ViewportHeight = 0.0f;
            Math::Mat4 ViewProjectionMatrix; // CRITICAL FIX: Storage for the matrix calculated by SetCamera
            Math::Vec4 ClearColor;

            uint32_t ShaderID = 0;

            // --- Batching Data ---
            uint32_t QuadVAO = 0;
            uint32_t QuadVBO = 0;
            uint32_t QuadEBO = 0;

            uint32_t IndexCount = 0;
            Vertex* VertexBufferBase = nullptr; // CRITICAL FIX: Base for memory allocation
            Vertex* VertexBufferPtr = nullptr;

            // Texture Management
            std::array<uint32_t, MAX_TEXTURE_SLOTS> TextureSlots; // OpenGL IDs
            uint32_t TextureSlotIndex = 1; // Slot 0 is reserved for the white texture
            uint32_t WhiteTextureID = 0;
        };

        static RendererData* s_Data; // Ptr to the static data

        // Internal helper functions
        // CRITICAL FIX: This signature matches the implementation now
        static uint32_t CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc);

        // These declarations were missing implementations in the provided Renderer.cpp
        static void Flush();
        static void LoadDefaultAssets();
    };

}

#endif //ZUESENGINE_RENDERER_H