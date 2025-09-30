#ifndef ZUESENGINE_RENDERER_H
#define ZUESENGINE_RENDERER_H

#include <cstdint> // For uint32_t
#include <array>   // For std::array
#include <string>
#include <unordered_map>
#include <vector>

#include "Math.h"

// Forward declaration of the internal stb_truetype struct
// to make function signatures in the header possible.
// This struct is fully defined when STB_TRUETYPE_IMPLEMENTATION is used in the CPP file.
struct stbtt_quad {
    float x0, y0, s0, t0; // Top-Left position and UV
    float x1, y1, s1, t1; // Bottom-Right position and UV
};

// Include the stb_truetype header for the definition of stbtt_bakedchar
#include "../stb/stb_truetype.h"
// Removed the unused include: #include "../include/stb/stb_easy_font.h"

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


    // --- TEXTURE/FONT ATLAS CONSTANTS ---
    static constexpr uint32_t FONT_ATLAS_WIDTH = 512;
    static constexpr uint32_t FONT_ATLAS_HEIGHT = 512;
    static constexpr uint32_t FIRST_CHAR = 32; // First printable ASCII character (' ')
    static constexpr uint32_t CHAR_COUNT = 96; // Total count of printable ASCII characters (32-127)

    struct Font {
        uint32_t AtlasTextureID = 0;
        float Size = 0.0f; // The pixel height the font was rendered at
        stbtt_bakedchar BakedChars[CHAR_COUNT]; // stb_truetype character metrics
        uint8_t* FontBuffer = nullptr; // Raw font data buffer (must remain allocated)
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

        // --- Text Rendering ---
        static uint32_t LoadFont(const std::string& path, float pixelHeight);
        static void DrawText(uint32_t fontID,
                             const std::string& text,
                             const Math::Vec2& position,
                             const Math::Vec4& color,
                             float scale = 1.0f); // Scale parameter moved to the end, made optional


    private:

        static std::vector<Font> s_Fonts; // Static storage for loaded fonts (ID is index + 1)

        // Internal helper to submit text quads directly to the vertex buffer
        // Now compiles because stbtt_quad is forward-declared
        static void SubmitTextQuad(const stbtt_aligned_quad& q, const Math::Vec4& color, uint32_t textureID);


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
            // ... (RendererData members) ...
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
        static uint32_t CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc);
        static void Flush();
        static void LoadDefaultAssets();
    };

}

#endif // ZUESENGINE_RENDERER_H