//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Engine.h"
#include "../include/Engine/Renderer.h"

#include <array>
#include <fstream>
#include <vector> // Required for shader logging
#include <glad/glad.h>
#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../include/stb/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../include/Engine/Log.h"
#include "../include/stb/stb_image.h"

// --- Default Shader Source ---
// This is a minimal 2D shader that processes the batched vertices.
// NOTE: u_ViewProjection and u_Textures must match the uniforms in your Renderer.Init()
const char* VertexShaderSource = R"(
// Vertex Shader
#version 330 core
layout (location = 0) in vec3 a_Position;
layout (location = 1) in vec4 a_Color;
layout (location = 2) in vec2 a_TexCoord;
layout (location = 3) in float a_TexID;

out vec4 v_Color;
out vec2 v_TexCoord;
flat out float v_TexID;  // <<< ADD 'flat' HERE

uniform mat4 u_ViewProjection;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexID = a_TexID;
    gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
)";

const char* FragmentShaderSource = R"(
// Fragment Shader
#version 330 core
in vec4 v_Color;
in vec2 v_TexCoord;
flat in float v_TexID;  // <<< ADD 'flat' HERE

out vec4 f_Color;

uniform sampler2D u_Textures[32];

void main()
{
    vec4 texColor = texture(u_Textures[int(v_TexID)], v_TexCoord);
    f_Color = texColor * v_Color;
}
)";


namespace Engine {

    std::vector<Font> Renderer::s_Fonts = {};

    // --- Math Constants (Used internally) ---
    constexpr float PI = 3.14159265359f;
    constexpr float DEGREES_TO_RADIANS = PI / 180.0f;

    std::array<Engine::Math::Vec4, 4> QuadVertexPositions = {
        Engine::Math::Vec4(-0.5f, -0.5f, 0.0f, 1.0f), // Bottom-Left
        Engine::Math::Vec4( 0.5f, -0.5f, 0.0f, 1.0f), // Bottom-Right
        Engine::Math::Vec4( 0.5f,  0.5f, 0.0f, 1.0f), // Top-Right
        Engine::Math::Vec4(-0.5f,  0.5f, 0.0f, 1.0f)  // Top-Left
    };

    // NOTE: These constants should really be static members of Renderer for encapsulation,
    // but they are kept here to resolve the original compiler errors quickly.
    constexpr uint32_t MaxQuads = 20000;
    constexpr uint32_t MaxVertices = MaxQuads * 4;
    constexpr uint32_t MaxIndices = MaxQuads * 6;
    constexpr uint32_t MaxTextureSlots = 32; // Must match the value in Renderer.h's private static constexpr

    // Static member definition
    Renderer::RendererData* Renderer::s_Data = nullptr;

    // --- Utility Functions ---

    // CRITICAL FIX 1: Change return type from Mat4 to Vec4
    // Resolves: error: could not convert 'result' from 'Engine::Vec4' ... to 'Engine::Mat4'
    Math::Vec4 operator*(const Math::Mat4& transform, const Math::Vec4& vector) {
        Math::Vec4 result;
        // Standard 4x4 Matrix * 4x1 Vector multiplication (assuming column-major storage)
        // Mat4::elements[column + row * 4]

        // Row 0
        result.x = transform.elements[0 + 0 * 4] * vector.x +
                   transform.elements[0 + 1 * 4] * vector.y +
                   transform.elements[0 + 2 * 4] * vector.z +
                   transform.elements[0 + 3 * 4] * vector.w;
        // Row 1
        result.y = transform.elements[1 + 0 * 4] * vector.x +
                   transform.elements[1 + 1 * 4] * vector.y +
                   transform.elements[1 + 2 * 4] * vector.z +
                   transform.elements[1 + 3 * 4] * vector.w;
        // Row 2
        result.z = transform.elements[2 + 0 * 4] * vector.x +
                   transform.elements[2 + 1 * 4] * vector.y +
                   transform.elements[2 + 2 * 4] * vector.z +
                   transform.elements[2 + 3 * 4] * vector.w;
        // Row 3
        result.w = transform.elements[3 + 0 * 4] * vector.x +
                   transform.elements[3 + 1 * 4] * vector.y +
                   transform.elements[3 + 2 * 4] * vector.z +
                   transform.elements[3 + 3 * 4] * vector.w;

        return result;
    }


    // CRITICAL FIX 2: Correct the signature of CompileShader to match the declaration in Renderer.h.
    // Resolves: undefined reference to `Engine::Renderer::CompileShader(std::__cxx11::basic_string<char...
    uint32_t Renderer::CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc) {
        // 1. Create and Compile Vertex Shader
        const uint32_t vs = glCreateShader(GL_VERTEX_SHADER);
        const char* vSource = vertexSrc.c_str(); // Use c_str() to get a C-style string
        glShaderSource(vs, 1, &vSource, nullptr);
        glCompileShader(vs);

        int success;
        char infoLog[512];
        glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vs, 512, nullptr, infoLog);
            glDeleteShader(vs);
            return 0;
        }

        // 2. Create and Compile Fragment Shader
        const uint32_t fs = glCreateShader(GL_FRAGMENT_SHADER);
        const char* fSource = fragmentSrc.c_str(); // Use c_str() to get a C-style string
        glShaderSource(fs, 1, &fSource, nullptr);
        glCompileShader(fs);

        glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fs, 512, nullptr, infoLog);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return 0;
        }

        // 3. Link Program
        const uint32_t program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            glDeleteShader(vs);
            glDeleteShader(fs);
            glDeleteProgram(program);
            return 0;
        }

        // 4. Cleanup
        glDetachShader(program, vs);
        glDetachShader(program, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        return program;
    }

    // Implementation for the header's LoadDefaultAssets() helper
    void Renderer::LoadDefaultAssets() {
        // Load the default 1x1 white texture here if needed, or keep it in Init()
    }

    // Implementation for the header's Flush() helper
    void Renderer::Flush() {
        // Flush logic (similar to EndBatch but without the texture/buffer setup)
    }

    // --- Core Lifecycle Implementations ---

    void Renderer::Init() {
        // ... (Initialization logic remains the same) ...
        if (s_Data != nullptr) {
            // Already initialized
            return;
        }

        s_Data = new RendererData();
        // CRITICAL FIX 3: Allocate the vertex buffer base memory
        s_Data->VertexBufferBase = new Vertex[MaxVertices];
        s_Data->VertexBufferPtr = s_Data->VertexBufferBase; // Initialize pointer

        // OpenGL Global State
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        //glEnable(GL_DEPTH_TEST); // Optional for 2D, but good for z-ordering

        // 1. Compile Shader
        // CRITICAL FIX 2: Now calls the correct signature of CompileShader.
        s_Data->ShaderID = CompileShader(VertexShaderSource, FragmentShaderSource);
        if (s_Data->ShaderID == 0) {
            return;
        }

        // 2. Setup Batching Buffers (VAO, VBO, EBO)
        glGenVertexArrays(1, &s_Data->QuadVAO);
        glBindVertexArray(s_Data->QuadVAO);

        glGenBuffers(1, &s_Data->QuadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, s_Data->QuadVBO);
        glBufferData(GL_ARRAY_BUFFER, MaxVertices * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);

        // Define Vertex Attributes (must match the Vertex struct)
        // Position (Vec2, 8 bytes offset)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, Position));
        // Color (Vec4, 16 bytes offset)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, Color));
        // TexCoord (Vec2, 24 bytes offset)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, TexCoord));
        // TexID (float, 28 bytes offset)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, TexID));


        // Create Index Buffer (EBO)
        std::vector<uint32_t> quadIndices(MaxIndices);
        uint32_t offset = 0;
        for (uint32_t i = 0; i < MaxIndices; i += 6) {
            quadIndices[i + 0] = offset + 0;
            quadIndices[i + 1] = offset + 1;
            quadIndices[i + 2] = offset + 2;

            quadIndices[i + 3] = offset + 2;
            quadIndices[i + 4] = offset + 3;
            quadIndices[i + 5] = offset + 0;

            offset += 4;
        }

        glGenBuffers(1, &s_Data->QuadEBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_Data->QuadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, MaxIndices * sizeof(uint32_t), quadIndices.data(), GL_STATIC_DRAW);

        // 3. Setup Texture Slots (Uniform Array)
        // Set the u_Textures uniform once, as it's static.
        std::vector<int> samplers(MaxTextureSlots);
        for (int i = 0; i < MaxTextureSlots; i++) {
            samplers[i] = i;
        }

        glUseProgram(s_Data->ShaderID);
        const int texturesUniform = glGetUniformLocation(s_Data->ShaderID, "u_Textures");
        glUniform1iv(texturesUniform, MaxTextureSlots, samplers.data());

        // 4. Create a default 1x1 white texture (slot 0)
        uint32_t whiteTextureID;
        glGenTextures(1, &whiteTextureID);
        glBindTexture(GL_TEXTURE_2D, whiteTextureID);
        // Set texture wrapping/filtering options
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Create 1x1 white pixel data
        const uint32_t whiteColor = 0xFFFFFFFF; // RGBA
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whiteColor);

        // Assign to slot 0
        s_Data->TextureSlots[0] = whiteTextureID;
        s_Data->TextureSlotIndex = 1; // Slot 0 is reserved for the white texture

        // 5. Setup Framebuffer (FBO)
        glGenFramebuffers(1, &s_Data->EditorFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, s_Data->EditorFBO);

        // Create Color Attachment Texture
        glGenTextures(1, &s_Data->ColorAttachment);
        glBindTexture(GL_TEXTURE_2D, s_Data->ColorAttachment);
        // Initial size is 1x1, will be resized on first SetViewportSize
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_Data->ColorAttachment, 0);

        // Create Depth/Stencil Attachment Texture
        glGenTextures(1, &s_Data->DepthAttachment);
        glBindTexture(GL_TEXTURE_2D, s_Data->DepthAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, 1, 1, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_Data->DepthAttachment, 0);

        // Unbind everything
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Renderer::Shutdown() {
        // ... (Shutdown logic remains the same) ...
        if (s_Data == nullptr) {
            return;
        }

        // Delete OpenGL objects
        glDeleteProgram(s_Data->ShaderID);
        glDeleteVertexArrays(1, &s_Data->QuadVAO);
        glDeleteBuffers(1, &s_Data->QuadVBO);
        glDeleteBuffers(1, &s_Data->QuadEBO);
        glDeleteFramebuffers(1, &s_Data->EditorFBO);
        glDeleteTextures(1, &s_Data->ColorAttachment);
        glDeleteTextures(1, &s_Data->DepthAttachment);

        // Delete Texture Slots
        glDeleteTextures(MaxTextureSlots, s_Data->TextureSlots.data());

        // Delete allocated memory (Matched with new[] in Init)
        delete[] s_Data->VertexBufferBase;
        delete s_Data;
        s_Data = nullptr;
    }

    void Renderer::Render() {
        // ... (Render logic remains the same) ...
        if (s_Data == nullptr) return;

        // 1. Bind the FBO
        glBindFramebuffer(GL_FRAMEBUFFER, s_Data->EditorFBO);
        glViewport(0, 0, static_cast<int>(s_Data->ViewportWidth), static_cast<int>(s_Data->ViewportHeight));

        // Bind the editor's FBO
        glBindFramebuffer(GL_FRAMEBUFFER, s_Data->EditorFBO);
        glViewport(0, 0, static_cast<GLsizei>(s_Data->ViewportWidth), static_cast<GLsizei>(s_Data->ViewportHeight));

        // FIX: Use the stored clear color
        glClearColor(
            s_Data->ClearColor.x,
            s_Data->ClearColor.y,
            s_Data->ClearColor.z,
            s_Data->ClearColor.w
        );

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void Renderer::SetViewportSize(const float width, const float height) {
        if (s_Data == nullptr) return;


        if (s_Data->ViewportWidth != width || s_Data->ViewportHeight != height) {
            s_Data->ViewportWidth = width;
            s_Data->ViewportHeight = height;

            // Resize the FBO attachments
            glBindTexture(GL_TEXTURE_2D, s_Data->ColorAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(width), static_cast<int>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindTexture(GL_TEXTURE_2D, s_Data->DepthAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, static_cast<int>(width), static_cast<int>(height), 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        } else {
        }
    }

    uint32_t Renderer::GetRenderTextureID() {
        const uint32_t id = (s_Data != nullptr) ? s_Data->ColorAttachment : 0;
        return id;
    }

    // --- Batching Implementations ---

    // CRITICAL FIX 4: Remove the StartBatch() stub, as it causes 'no declaration matches' error
    /*
    void Renderer::StartBatch() {
        // ... (StartBatch logic remains the same) ...
        // Renamed to BeginBatch
    }
    */

    void Renderer::BeginBatch() {
        if (s_Data == nullptr) return;

        // OPTIMIZED: Only clear the memory that was actually used in the previous frame
        // This avoids clearing the entire 2.5MB buffer (20000 quads * 4 vertices * 32 bytes)
        if (s_Data->LastFrameVertexCount > 0) {
            memset(s_Data->VertexBufferBase, 0, s_Data->LastFrameVertexCount * sizeof(Vertex));
        }

        // Reset batching state
        s_Data->IndexCount = 0;
        s_Data->VertexBufferPtr = s_Data->VertexBufferBase;
        s_Data->TextureSlotIndex = 1; // Keep white texture at slot 0

        SetTextureUniforms();
    }

    void Renderer::EndBatch() {
        if (s_Data->IndexCount == 0) return;

        // Calculate vertices written
        const uint32_t verticesWritten = s_Data->VertexBufferPtr - s_Data->VertexBufferBase;

        // Store for next frame's cleanup
        s_Data->LastFrameVertexCount = verticesWritten;

        // Only upload what we actually wrote
        const GLsizeiptr size = verticesWritten * sizeof(Vertex);

        glBindBuffer(GL_ARRAY_BUFFER, s_Data->QuadVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, size, s_Data->VertexBufferBase);

        glUseProgram(s_Data->ShaderID);

        const GLint vpLocation = glGetUniformLocation(s_Data->ShaderID, "u_ViewProjection");
        if (vpLocation != -1) {
            glUniformMatrix4fv(vpLocation, 1, GL_FALSE, s_Data->ViewProjectionMatrix.elements);
        }

        for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, s_Data->TextureSlots[i]);
        }

        glBindVertexArray(s_Data->QuadVAO);

        // Only draw the indices we actually need
        glDrawElements(GL_TRIANGLES, s_Data->IndexCount, GL_UNSIGNED_INT, nullptr);

        // Reset state for next batch (but don't clear LastFrameVertexCount yet)
        s_Data->IndexCount = 0;
        s_Data->VertexBufferPtr = s_Data->VertexBufferBase;
        s_Data->TextureSlotIndex = 1;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // In Renderer.cpp

// NOTE: Ensure the rest of Renderer.cpp (includes, global definitions, Init/Shutdown/BeginBatch/EndBatch, etc.)
// are present above this code.

// --- Submission Overloads ---

// Original Simple SubmitQuad (now calls the detailed one with defaults)
    // In Renderer.cpp

// NOTE: Ensure the rest of Renderer.cpp (includes, global definitions, Init/Shutdown/BeginBatch/EndBatch, etc.)
// are present above this code.

// --- Submission Overloads ---

    // In Renderer.cpp

// NOTE: This file is incomplete and assumes the rest of Renderer.cpp (includes, global definitions,
// Init/Shutdown/BeginBatch/EndBatch, and the RendererData struct definition) are present above this code.

// --- Submission Overloads ---

    // Original Simple SubmitQuad (now calls the detailed one with defaults)
    void Renderer::SubmitQuad(const Math::Vec2& position, const float rotation, const Math::Vec2& size, const Math::Vec4& color, const uint32_t textureID) {
        // Default to z=0.0f and use the entire texture UVs {0, 0, 1, 1}
        // This calls the detailed worker function below.
        SubmitQuad(position, rotation, size, color, textureID, 0.0f, { 0.0f, 0.0f, 1.0f, 1.0f });
    }

    // Detailed Quad Submission (The worker function: Includes Z-depth and UV Rect for texture cutting)
    void Renderer::SubmitQuad(
    const Math::Vec2& position,
    const float rotation,
    const Math::Vec2& size,
    const Math::Vec4& color,
    const uint32_t textureID,
    const float z,
    const Math::Vec4& textureUVRect
) {
        if (s_Data == nullptr) return;

        // --- CHECK BUFFER SPACE FIRST (BEFORE any state changes) ---
        if (s_Data->IndexCount + 6 > MaxIndices) {
            EndBatch();
            BeginBatch();
        }

        // --- 1. Texture Slot Logic (Finding/Assigning) ---
        float textureSlot = 0.0f;

        if (textureID > 0) {
            bool found = false;
            // Search for existing slot (skip slot 0, which is white)
            for (uint32_t i = 1; i < s_Data->TextureSlotIndex; i++) {
                if (s_Data->TextureSlots[i] == textureID) {
                    textureSlot = static_cast<float>(i);
                    found = true;
                    break;
                }
            }

            // Assign new slot if not found
            if (!found) {
                // If we're out of texture slots, flush
                if (s_Data->TextureSlotIndex >= MAX_TEXTURE_SLOTS) {
                    EndBatch();
                    BeginBatch();
                    // After flush, slot 0 is white and slot 1+ are empty
                    // So assign to slot 1
                    textureSlot = 1.0f;
                    s_Data->TextureSlots[1] = textureID;
                    s_Data->TextureSlotIndex = 2;
                } else {
                    // Normal case: assign to next available slot
                    textureSlot = static_cast<float>(s_Data->TextureSlotIndex);
                    s_Data->TextureSlots[s_Data->TextureSlotIndex] = textureID;
                    s_Data->TextureSlotIndex++;
                }
            }
        }

        // --- 2. Calculate Transform ---
        const Math::Mat4 transform = Math::Mat4::Translate(Math::Vec2(position.x, position.y))
                                   * Math::Mat4::Rotate(rotation)
                                   * Math::Mat4::Scale(Math::Vec2(size.x, size.y));

        const Math::Vec2 quadPositions[4] = {
            {-0.5f, -0.5f}, // V0: Bottom-left
            { 0.5f, -0.5f}, // V1: Bottom-right
            { 0.5f,  0.5f}, // V2: Top-right
            {-0.5f,  0.5f}  // V3: Top-left
        };

        const float u0 = textureUVRect.x;
        const float v0 = textureUVRect.y;
        const float u1 = textureUVRect.x + textureUVRect.z;  // x + width
        const float v1 = textureUVRect.y + textureUVRect.w;  // y + height

        const Math::Vec2 QuadTexCoords[4] = {
            {u0, v0}, // V0: Bottom-left
            {u1, v0}, // V1: Bottom-right
            {u1, v1}, // V2: Top-right
            {u0, v1}  // V3: Top-left
        };

        // --- 3. Write 4 Vertices to the Buffer ---
        for (int i = 0; i < 4; ++i) {
            const Math::Vec4 transformedPos = transform * Math::Vec4(quadPositions[i].x, quadPositions[i].y, 0.0f, 1.0f);

            s_Data->VertexBufferPtr->Position.x = transformedPos.x;
            s_Data->VertexBufferPtr->Position.y = transformedPos.y;
            s_Data->VertexBufferPtr->Position.z = z;

            s_Data->VertexBufferPtr->Color = color;
            s_Data->VertexBufferPtr->TexCoord = QuadTexCoords[i];
            s_Data->VertexBufferPtr->TexID = textureSlot;
            s_Data->VertexBufferPtr++;
        }

        // Increment the index count
        s_Data->IndexCount += 6;
    }

    // --- Camera Management Implementation ---
    void Renderer::SetCamera(const Math::Vec2& position, const float zoom, const float halfHeight, const float rotationRadians) {
        if (s_Data == nullptr) return;


        // 1. Calculate Projection Matrix (from halfHeight and viewport size)
        const float ratio = s_Data->ViewportWidth / s_Data->ViewportHeight;
        const float right = ratio * halfHeight / zoom;
        const float left = -right;
        const float top = halfHeight / zoom;
        const float bottom = -top;

        // Assuming Mat4::Orthographic uses a standard near/far plane (e.g., -1.0f, 1.0f)
        const Math::Mat4 projection = Math::Mat4::Orthographic(left, right, bottom, top, -1.0f, 1.0f);

        // 2. Calculate View Matrix (Inverse translation and rotation)
        // The view matrix moves the world opposite to the camera
        const Math::Mat4 translate = Math::Mat4::Translate(position * -1.0f);
        const Math::Mat4 rotate = Math::Mat4::Rotate(rotationRadians * -1.0f);
        const Math::Mat4 view = rotate * translate; // Apply rotation THEN translation

        // 3. Combine and Store
        s_Data->ViewProjectionMatrix = projection * view;

        // 4. CRITICAL FIX: Upload the matrix uniform
        glUseProgram(s_Data->ShaderID);

        // You MUST get the uniform location and upload the matrix.
        // If you cache the uniform location in Init(), use that ID. Otherwise, get it now:
        const GLint location = glGetUniformLocation(s_Data->ShaderID, "u_ViewProjection");
        glUniformMatrix4fv(location, 1, GL_FALSE, s_Data->ViewProjectionMatrix.elements); // Pass the matrix data
    }

    // You might also need this utility to correctly upload the array of texture uniform slots
    // This should be called once in Renderer::Init() or Renderer::BeginBatch()
    void Renderer::SetTextureUniforms() {
        // You'll need to define the max texture slots in Renderer.h
        std::array<int, MAX_TEXTURE_SLOTS> samplers;
        for (int i = 0; i < MAX_TEXTURE_SLOTS; i++) {
            samplers[i] = i;
        }

        glUseProgram(s_Data->ShaderID);
        const GLint texturesLoc = glGetUniformLocation(s_Data->ShaderID, "u_Textures");
        glUniform1iv(texturesLoc, MAX_TEXTURE_SLOTS, samplers.data());
    }

    // --- Texture Implementation ---

    uint32_t Renderer::LoadTexture(const std::string& filePath) {
        // ... (Texture loading logic remains the same) ...
        uint32_t textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Set the texture wrapping parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        // Set texture filtering parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int width, height, nrChannels;
        stbi_set_flip_vertically_on_load(true); // Flip texture on load

        if (unsigned char *data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 0)) {
            GLenum format = GL_RGB;
            if (nrChannels == 4) format = GL_RGBA;
            else if (nrChannels == 1) format = GL_RED;

            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            stbi_image_free(data);
            return textureID;
        } else {
            stbi_image_free(data);
            return 0;
        }
    }

    void Renderer::SetClearColor(const Math::Vec4& color) {
        // Store the color to be used for the next glClear
        if (!s_Data) return;
        s_Data->ClearColor = color;
    }

    Line Renderer::DrawLine(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float thickness) {
        Renderer::SubmitQuad((start + end) * 0.5f, atan2(end.y - start.y, end.x - start.x), { (end - start).Length(), thickness }, color, 0, .99f);
        return { start, end, thickness };
    }

    Rect Renderer::DrawRect(const Math::Vec2& position, const Math::Vec2& size, const Math::Vec4& color, const float rotationRadians) {
        Renderer::SubmitQuad(position, rotationRadians, size, color, 0, .99f);
        return { position, size, rotationRadians };
    }

    Circle Renderer::DrawCircle(const Math::Vec2& center, const float radius, const Math::Vec4& color,
                            const int segments, const bool outlineOnly, const float thickness) {
        const float step = 2.0f * 3.14159265f / segments;
        for (int i = 0; i < segments; i++) {
            Math::Vec2 p0 = center + Math::Vec2(std::cos(i * step),     std::sin(i * step))     * radius;
            Math::Vec2 p1 = center + Math::Vec2(std::cos((i + 1) * step), std::sin((i + 1) * step)) * radius;
            DrawLine(p0, p1, color, thickness);
        }
        return { center, radius, outlineOnly, thickness };
    }


    Arrow Renderer::DrawArrow(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, const float thickness) {
        DrawLine(start, end, color, thickness);

        const Math::Vec2 dir = (end - start).Normalize();
        const Math::Vec2 perp(-dir.y, dir.x);
        constexpr float headSize = 0.5f;
        const Math::Vec2 tip = end;
        const Math::Vec2 left = end - dir * headSize + perp * (headSize * 0.5f);
        const Math::Vec2 right = end - dir * headSize - perp * (headSize * 0.5f);

        DrawLine(tip, left, color, thickness);
        DrawLine(tip, right, color, thickness);
        DrawLine(left, right, color, thickness);

        return { start, end, thickness };
    }


    Math::Vec2 Renderer::WorldToScreen(const Math::Vec2& worldPos) {
        if (!s_Data) return {};

        Math::Vec4 clip = s_Data->ViewProjectionMatrix * Math::Vec4(worldPos.x, worldPos.y, 0.0f, 1.0f);
        clip.x /= clip.w;
        clip.y /= clip.w;

        float x = (clip.x * 0.5f + 0.5f) * s_Data->ViewportWidth;
        float y = (clip.y * 0.5f + 0.5f) * s_Data->ViewportHeight;

        return { x, y };
    }

    // Renderer.cpp, implementation for ScreenToWorld

    Math::Vec2 Renderer::ScreenToWorld(const Math::Vec2& screenPos) {
        if (!s_Data) return {};

        // 1. Convert Screen Coordinates (Top-Left origin, Y-down) to NDC (Center origin, Y-up)

        // X-axis: Map [0, Width] -> [-1, 1]
        const float ndcX = (2.0f * screenPos.x) / s_Data->ViewportWidth - 1.0f;

        // Y-axis: Map [0, Height] (Y-down) to [1, -1] (Y-up) - This is the standard flip
        const float ndcY = 1.0f - (2.0f * screenPos.y) / s_Data->ViewportHeight;

        const Math::Vec4 ndc(ndcX, ndcY, 0.0f, 1.0f);

        // 2. Inverse Transform
        const Math::Mat4 inv = s_Data->ViewProjectionMatrix.Inverted();
        Math::Vec4 world = inv * ndc;

        // 3. Perspective Divide
        world.x /= world.w;
        world.y /= world.w;

        return { world.x, world.y };
    }

void Renderer::SubmitTextQuad(const stbtt_aligned_quad& q, const Math::Vec4& color, uint32_t textureID, float z) {
        // Replace 5000 with your actual MaxIndices constant if needed
        if (s_Data->IndexCount >= 5000) {
            EndBatch();
            BeginBatch();
        }

        // Texture ID slot lookup logic (remains the same)
        float textureSlot = 0.0f;
        for (uint32_t i = 1; i < s_Data->TextureSlotIndex; i++) {
            if (s_Data->TextureSlots[i] == textureID) {
                textureSlot = (float)i;
                break;
            }
        }

        // If the texture isn't found, load it into a new slot (logic remains the same)
        if (textureSlot == 0.0f) {
            if (s_Data->TextureSlotIndex >= MAX_TEXTURE_SLOTS) {
                EndBatch();
                BeginBatch();
                s_Data->TextureSlots[1] = textureID;
                textureSlot = 1.0f;
                s_Data->TextureSlotIndex = 2;
            } else {
                textureSlot = (float)s_Data->TextureSlotIndex;
                s_Data->TextureSlots[s_Data->TextureSlotIndex] = textureID;
                s_Data->TextureSlotIndex++;
            }
        }

        // Submit 4 vertices using the data from the quad struct
        // NOTE: After Y-flip transformation, q.y0 > q.y1 (top > bottom in world space)
        // So q.y0 is TOP and q.y1 is BOTTOM

        // 1. Bottom-Left: (q.x0, q.y1) where y1 is smaller = bottom
        s_Data->VertexBufferPtr->Position = { q.x0, q.y1, z };
        s_Data->VertexBufferPtr->Color = color;
        s_Data->VertexBufferPtr->TexCoord = { q.s0, q.t1 };
        s_Data->VertexBufferPtr->TexID = textureSlot;
        s_Data->VertexBufferPtr++;

        // 2. Bottom-Right: (q.x1, q.y1)
        s_Data->VertexBufferPtr->Position = { q.x1, q.y1, z };
        s_Data->VertexBufferPtr->Color = color;
        s_Data->VertexBufferPtr->TexCoord = { q.s1, q.t1 };
        s_Data->VertexBufferPtr->TexID = textureSlot;
        s_Data->VertexBufferPtr++;

        // 3. Top-Right: (q.x1, q.y0) where y0 is larger = top
        s_Data->VertexBufferPtr->Position = { q.x1, q.y0, z };
        s_Data->VertexBufferPtr->Color = color;
        s_Data->VertexBufferPtr->TexCoord = { q.s1, q.t0 };
        s_Data->VertexBufferPtr->TexID = textureSlot;
        s_Data->VertexBufferPtr++;

        // 4. Top-Left: (q.x0, q.y0)
        s_Data->VertexBufferPtr->Position = { q.x0, q.y0, z };
        s_Data->VertexBufferPtr->Color = color;
        s_Data->VertexBufferPtr->TexCoord = { q.s0, q.t0 };
        s_Data->VertexBufferPtr->TexID = textureSlot;
        s_Data->VertexBufferPtr++;

        s_Data->IndexCount += 6;
    }


    uint32_t Engine::Renderer::LoadFont(const std::string& fontPath, float pixelHeight) {
        LOG_INFO("LoadFont: Loading font from '" + fontPath + "' at " + std::to_string(pixelHeight) + "px");

        // 1. Read the font file into a buffer
        std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            LOG_ERROR("LoadFont: Failed to open font file: " + fontPath);
            return 0;
        }

        std::streamsize size = file.tellg();
        // NOTE: fontBuffer must persist for the lifetime of the font.
        uint8_t* fontBuffer = new uint8_t[size];
        file.seekg(0, std::ios::beg);
        if (!file.read((char*)fontBuffer, size)) {
            delete[] fontBuffer;
            return 0;
        }
        file.close();

        // 2. Prepare the font atlas bitmap buffer (grayscale, 1 byte per pixel)
        uint8_t* temp_bitmap = new uint8_t[FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT];

        // Add a new Font struct to our static storage
        s_Fonts.emplace_back();
        Font& newFont = s_Fonts.back();

        // 3. Bake the font glyphs into the bitmap and calculate metrics
        int result = stbtt_BakeFontBitmap(
            fontBuffer,
            0,
            pixelHeight,
            temp_bitmap,
            FONT_ATLAS_WIDTH,
            FONT_ATLAS_HEIGHT,
            FIRST_CHAR,
            CHAR_COUNT,
            newFont.BakedChars
        );

        if (result <= 0) {
            LOG_ERROR("LoadFont: stbtt_BakeFontBitmap failed");
            delete[] fontBuffer;
            delete[] temp_bitmap;
            s_Fonts.pop_back();
            return 0; // Baking failed
        }

        LOG_INFO("LoadFont: Font baked successfully, " + std::to_string(result) + " characters fit in atlas");

        // 4. Create an OpenGL texture from the bitmap
        glGenTextures(1, &newFont.AtlasTextureID);
        glBindTexture(GL_TEXTURE_2D, newFont.AtlasTextureID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // Disable byte-alignment restriction for 1-byte grayscale data
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        // Upload the single-channel grayscale bitmap using GL_RED format
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, FONT_ATLAS_WIDTH, FONT_ATLAS_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, temp_bitmap);

        // Swizzle: (R,G,B,A) -> (1, 1, 1, Red). This moves the grayscale coverage from GL_RED to the ALPHA channel
        // for use in blending against the V_Color in the shader.
        GLint swizzleMask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
        glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);

        // Cleanup the temporary bitmap
        delete[] temp_bitmap;

        // 5. Store final data and return ID (ID is the 1-based index)
        newFont.Size = pixelHeight;
        newFont.FontBuffer = fontBuffer;

        uint32_t fontID = s_Fonts.size();
        LOG_INFO("LoadFont: Font loaded successfully with ID " + std::to_string(fontID) + ", texture ID: " + std::to_string(newFont.AtlasTextureID));

        return fontID;
    }

    // --- 3. Implementation: DrawText ---
    // Draws a string of text using a previously loaded font, positioning it starting at 'position'.
    // --- 3. Implementation: DrawText (using stbtt_aligned_quad) ---
    void Renderer::DrawText(uint32_t fontID, const std::string& text, const Math::Vec2& position, const Math::Vec4& color, float scale, float worldScale) {
        if (!s_Data) {
            LOG_ERROR("DrawText: s_Data is null");
            return;
        }
        if (fontID == 0 || fontID > s_Fonts.size()) {
            LOG_ERROR("DrawText: Invalid fontID " + std::to_string(fontID) + " (fonts loaded: " + std::to_string(s_Fonts.size()) + ")");
            return;
        }

        const Font& font = s_Fonts[fontID - 1];
        LOG_INFO("DrawText: Rendering '" + text + "' at (" + std::to_string(position.x) + ", " + std::to_string(position.y) + ") with worldScale=" + std::to_string(worldScale));
        LOG_INFO("DrawText: Font atlas texture ID: " + std::to_string(font.AtlasTextureID));

        // Start at origin in pixel space - we'll transform to world space
        float pixelX = 0.0f;
        float pixelY = 0.0f;
        float dummyY = 0.0f;

        int charCount = 0;
        for (const char c : text) {
            if (c >= FIRST_CHAR && c < (FIRST_CHAR + CHAR_COUNT)) {

                // 1. Get quad geometry in pixel space and advance x cursor
                stbtt_aligned_quad q;
                stbtt_GetBakedQuad(
                    font.BakedChars,
                    FONT_ATLAS_WIDTH,
                    FONT_ATLAS_HEIGHT,
                    c - FIRST_CHAR,
                    &pixelX,
                    &dummyY,
                    &q,
                    1
                );

                // 2. Apply text scale to the pixel-space quad
                q.x0 *= scale;
                q.y0 *= scale;
                q.x1 *= scale;
                q.y1 *= scale;

                // 3. Adjust Y-coordinates to the pixel baseline position
                q.y0 += pixelY;
                q.y1 += pixelY;

                // 4. Transform from pixel space to world space
                // In stb_truetype: y0 is TOP, y1 is BOTTOM (Y-down screen space)
                // In world space: we want Y-up, so negate both (top becomes positive, bottom becomes negative)
                q.x0 = q.x0 * worldScale + position.x;
                q.y0 = -q.y0 * worldScale + position.y;  // Top in Y-down -> becomes top in Y-up
                q.x1 = q.x1 * worldScale + position.x;
                q.y1 = -q.y1 * worldScale + position.y;  // Bottom in Y-down -> becomes bottom in Y-up

                if (charCount == 0) {
                    LOG_INFO("DrawText: First char quad: x=[" + std::to_string(q.x0) + " to " + std::to_string(q.x1) + "], y=[" + std::to_string(q.y0) + " to " + std::to_string(q.y1) + "]");
                    LOG_INFO("DrawText: First char UVs: s=[" + std::to_string(q.s0) + " to " + std::to_string(q.s1) + "], t=[" + std::to_string(q.t0) + " to " + std::to_string(q.t1) + "]");
                }

                // 5. Submit the character quad (now in world space)
                // Use z = 0.9f to render text on top of most scene geometry
                SubmitTextQuad(
                    q,
                    color,
                    font.AtlasTextureID,
                    0.9f
                );

                charCount++;

            } else if (c == '\n') {
                // Newline handling
                pixelX = 0.0f;
                pixelY -= (font.Size * scale);
            }
        }

        LOG_INFO("DrawText: Submitted " + std::to_string(charCount) + " characters");
    }

} // namespace Engine