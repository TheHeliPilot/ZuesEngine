#ifndef ZUESENGINE_RENDERER_H
#define ZUESENGINE_RENDERER_H

#include "ZuesAPI.h"
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

    struct ZUES_API Line {
        Math::Vec2 start;
        Math::Vec2 end;
        float thickness;
    };

    struct ZUES_API Rect {
        Math::Vec2 position;
        Math::Vec2 size;
        float rotationRadians;
    };

    struct ZUES_API Circle {
        Math::Vec2 center;
        float radius;
        bool outlineOnly = false;
        float thickness = 1.0f;
        Math::Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct ZUES_API Arrow {
        Math::Vec2 start;
        Math::Vec2 end;
        float thickness;
    };

    struct ZUES_API Triangle {
        Math::Vec2 v0;
        Math::Vec2 v1;
        Math::Vec2 v2;
    };


    // --- TEXTURE/FONT ATLAS CONSTANTS ---
    static constexpr uint32_t FONT_ATLAS_WIDTH = 1024;
    static constexpr uint32_t FONT_ATLAS_HEIGHT = 1024;
    static constexpr uint32_t FIRST_CHAR = 32; // First printable ASCII character (' ')
    static constexpr uint32_t CHAR_COUNT = 96; // Total count of printable ASCII characters (32-127)

    struct ZUES_API Font {
        uint32_t AtlasTextureID = 0;
        float Size = 0.0f; // The pixel height the font was rendered at
        stbtt_bakedchar BakedChars[CHAR_COUNT]; // stb_truetype character metrics
        uint8_t* FontBuffer = nullptr; // Raw font data buffer (must remain allocated)
    };


    // --- Renderer Class ---

    class ZUES_API Renderer {
    public:
        // --- Core Lifecycle ---
        static void Init();
        static void Render();
        static void Shutdown();

        // --- 2D Batching Pipeline ---

        static void BeginBatch();
        static void EndBatch();

        // 1. Simple Quad Submission (calls the detailed version with z=0.0f and full UVs)
        static void SubmitQuad(
            const Math::Vec2& position,
            const float rotation,
            const Math::Vec2& size,
            const Math::Vec4& color,
            const uint32_t textureID
        );

        // 2. Detailed Quad Submission (The worker function: Includes Z-depth and UV Rect for texture cutting)
        static void SubmitQuad(
            const Math::Vec2& position,
            const float rotation,
            const Math::Vec2& size,
            const Math::Vec4& color,
            const uint32_t textureID,
            const float z,
            // The UV rectangle for the sub-texture: { u_min, v_min, u_max, v_max }
            const Math::Vec4& textureUVRect = { 0.0f, 0.0f, 1.0f, 1.0f } // Default is the whole texture
        );

        static void SubmitLine(const Line& line, const Math::Vec4& color, const float z);
        static void SubmitRect(const Rect& rect, const Math::Vec4& color, const float z);
        static void SubmitCircle(const Circle& circle, const Math::Vec4& color, const float z);

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
                             float scale = 1.0f, // Scale parameter moved to the end, made optional
                             float worldScale = 0.01f,
                             float rotation = 0); // World units per pixel (default: 100 pixels = 1 world unit)


    private:

        static std::vector<Font> s_Fonts; // Static storage for loaded fonts (ID is index + 1)

        // Internal helper to submit text quads directly to the vertex buffer
        // NOTE: The stbtt_aligned_quad type is used here, matching the stb_truetype function
        static void SubmitTextQuad(float x0, float y0,  // Bottom-Left
                                   float x1, float y1,  // Bottom-Right
                                   float x2, float y2,  // Top-Right
                                   float x3, float y3,  // Top-Left
                                   float s0, float t0, float s1, float t1,  // Texture coords
                                   const Math::Vec4& color,
                                   uint32_t textureID,
                                   float z);


        // 2D Vertex Structure for Batching
        struct Vertex {
            Math::Vec3 Position; // Engine::Math::Vec3
            Math::Vec4 Color;    // Engine::Math::Vec4
            Math::Vec2 TexCoord; // Engine::Math::Vec2
            float TexID{};   // Texture slot index (0.0 to 31.0)
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
            uint32_t LastFrameVertexCount = 0;
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

            // Pre-allocated vectors for glMultiDrawElements (to avoid per-frame allocations)
            std::vector<int> MultiDrawCounts;
            std::vector<const void*> MultiDrawIndices;
        };

        static RendererData* s_Data; // Ptr to the static data

        // Internal helper functions
        static uint32_t CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc);
        static void Flush();
        static void LoadDefaultAssets();
    };

}

#endif // ZUESENGINE_RENDERER_H
