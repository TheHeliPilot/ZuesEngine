#ifndef ZUESENGINE_RENDERER_H
#define ZUESENGINE_RENDERER_H

#include <cstdint> // For uint32_t
#include <array>   // For std::array
#include <string>

#include "Math.h"  // <--- NEW: Include the centralized math library

namespace Engine {

    // Use aliases to simplify access within the Renderer namespace
    using Vec2 = Math::Vec2;
    using Vec4 = Math::Vec4;
    using Mat4 = Math::Mat4;

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
            const Vec2& position,
            float rotationRadians, // Rotation in radians (fixed parameter name)
            const Vec2& size,
            const Vec4& color,
            uint32_t textureID // OpenGL texture handle (0 for pure color/white)
        );

        // --- Camera Management ---

        // Fix: Added the rotationRadians parameter to the declaration.
        // Updates the camera's view-projection matrix uniform in the shader.
        // halfHeight defines the size of the view volume (e.g., 10 world units vertically).
        static void SetCamera(const Vec2& position, float zoom, float halfHeight, float rotationRadians);

        static void SetTextureUniforms();

        // --- Editor Interface ---
        static uint32_t GetRenderTextureID();
        static void SetViewportSize(float width, float height);
        static uint32_t LoadTexture(const std::string& filePath);

    private:

        // 2D Vertex Structure for Batching
        struct Vertex {
            Vec2 Position; // Engine::Math::Vec2
            Vec4 Color;    // Engine::Math::Vec4
            Vec2 TexCoord; // Engine::Math::Vec2
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
            Mat4 ViewProjectionMatrix; // CRITICAL FIX: Storage for the matrix calculated by SetCamera

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