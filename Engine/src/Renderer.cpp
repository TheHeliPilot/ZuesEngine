//
// Created by bucka on 9/26/2025.
//

#include "../include/Engine/Renderer.h"

#include <array>
#include <fstream>
#include <iostream>
#include <vector> // Required for shader logging
#include <glad/glad.h>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "../include/stb/stb_image.h"

// --- Default Shader Source ---
// This is a minimal 2D shader that processes the batched vertices.
// NOTE: u_ViewProjection and u_Textures must match the uniforms in your Renderer.Init()
const char* VertexShaderSource = R"(
// ... (Shader source remains the same) ...
#version 330 core
layout (location = 0) in vec4 a_Position; // x, y, z, w (z is depth, w is homogeneous component)
layout (location = 1) in vec4 a_Color;
layout (location = 2) in vec2 a_TexCoord;
layout (location = 3) in float a_TexID;

out vec4 v_Color;
out vec2 v_TexCoord;
out float v_TexID;

uniform mat4 u_ViewProjection;

void main()
{
    v_Color = a_Color;
    v_TexCoord = a_TexCoord;
    v_TexID = a_TexID;
    gl_Position = u_ViewProjection * a_Position;
}
)";

const char* FragmentShaderSource = R"(
// ... (Shader source remains the same) ...
#version 330 core
in vec4 v_Color;
in vec2 v_TexCoord;
in float v_TexID;

out vec4 f_Color;

uniform sampler2D u_Textures[32]; // Must match MAX_TEXTURE_SLOTS

void main()
{
    // Sample the correct texture from the array based on v_TexID
    // The texture ID is a float, so we cast it to an integer for the array index.
    vec4 texColor = texture(u_Textures[int(v_TexID)], v_TexCoord);
    f_Color = v_Color * texColor;
}
)";

namespace Engine {

    // --- Math Constants (Used internally) ---
    constexpr float PI = 3.14159265359f;
    constexpr float DEGREES_TO_RADIANS = PI / 180.0f;

    // --- Batching Data and Constants ---
    // Moved the structs/constants from the local scope to match the private members of Renderer
    struct Vertex {
        Vec2 Position;
        Vec4 Color;
        Vec2 TexCoord;
        float TexID; // Texture slot ID (0-31)
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
    Vec4 operator*(const Mat4& transform, const Vec4& vector) {
        Vec4 result;
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
            std::cerr << "Vertex Shader Compilation Failed:\n" << infoLog << std::endl;
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
            std::cerr << "Fragment Shader Compilation Failed:\n" << infoLog << std::endl;
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
            std::cerr << "Shader Program Linking Failed:\n" << infoLog << std::endl;
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
        glEnable(GL_DEPTH_TEST); // Optional for 2D, but good for z-ordering

        // 1. Compile Shader
        // CRITICAL FIX 2: Now calls the correct signature of CompileShader.
        s_Data->ShaderID = CompileShader(VertexShaderSource, FragmentShaderSource);
        if (s_Data->ShaderID == 0) {
            std::cerr << "Renderer Init Failed: Shader compilation failed." << std::endl;
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
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)offsetof(Vertex, Position));
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
        uint32_t whiteColor = 0xFFFFFFFF; // RGBA
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

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cerr << "Framebuffer is not complete!" << std::endl;

        // Unbind everything
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void Renderer::Shutdown() {
        // ... (Shutdown logic remains the same) ...
        if (s_Data == nullptr) return;

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
        glViewport(0, 0, (int)s_Data->ViewportWidth, (int)s_Data->ViewportHeight);

        // 2. Clear buffers (Color and Depth/Stencil)
        glClearColor(0.8f, 0.2f, 0.8f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        // Rendering is done via ECS systems calling BeginBatch/EndBatch.
        // The main Render() function's role is just to manage the FBO/Viewport.

        // 3. Unbind the FBO
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Renderer::SetViewportSize(float width, float height) {
        if (s_Data == nullptr) return;

        if (s_Data->ViewportWidth != width || s_Data->ViewportHeight != height) {
            s_Data->ViewportWidth = width;
            s_Data->ViewportHeight = height;

            // Resize the FBO attachments
            glBindTexture(GL_TEXTURE_2D, s_Data->ColorAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)width, (int)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            glBindTexture(GL_TEXTURE_2D, s_Data->DepthAttachment);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, (int)width, (int)height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        }
    }

    uint32_t Renderer::GetRenderTextureID() {
        return (s_Data != nullptr) ? s_Data->ColorAttachment : 0;
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

        // Reset the data for a new batch
        s_Data->IndexCount = 0;
        s_Data->VertexBufferPtr = s_Data->VertexBufferBase;
        s_Data->TextureSlotIndex = 1; // Keep white texture at slot 0
    }

    void Renderer::EndBatch() {
        if (s_Data == nullptr) return;

        // Calculate the actual size of the data to upload
        const uint32_t dataSize = (uint32_t)((uint8_t*)s_Data->VertexBufferPtr - (uint8_t*)s_Data->VertexBufferBase);

        if (dataSize == 0) return;

        // 1. Upload the vertex data to the GPU VBO
        glBindBuffer(GL_ARRAY_BUFFER, s_Data->QuadVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, s_Data->VertexBufferBase);

        // 2. Bind Textures
        for (uint32_t i = 0; i < s_Data->TextureSlotIndex; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, s_Data->TextureSlots[i]);
        }

        // 3. Draw
        glUseProgram(s_Data->ShaderID);
        glBindVertexArray(s_Data->QuadVAO);
        // The index count is 6 indices per quad * number of quads.
        // We use glDrawElements to draw using the EBO.
        glDrawElements(GL_TRIANGLES, s_Data->IndexCount, GL_UNSIGNED_INT, nullptr);

        // Cleanup
        glBindVertexArray(0);
    }

    void Renderer::SubmitQuad(const Vec2& position, float rotation, const Vec2& size, const Vec4& color, uint32_t textureID) {
        if (s_Data == nullptr) return;

        // 1. Check if the buffer is full or if a new texture requires a draw call.
        // MaxIndices is 6 indices per quad * MaxQuads. If we don't have enough space for a new quad (6 indices), draw.
        if (s_Data->IndexCount >= MaxIndices) {
            EndBatch();
            BeginBatch();
        }

        // 2. Find/Assign Texture Slot:
        float textureSlot = 0.0f; // Default to 0 (white texture/pure color)

        if (textureID > 0) {
            bool found = false;
            // Search for existing slot
            for (uint32_t i = 1; i < s_Data->TextureSlotIndex; i++) {
                if (s_Data->TextureSlots[i] == textureID) {
                    textureSlot = (float)i;
                    found = true;
                    break;
                }
            }

            // Assign new slot if not found
            if (!found) {
                // If the next slot is beyond the limit, force a draw
                if (s_Data->TextureSlotIndex >= MaxTextureSlots) {
                    EndBatch();
                    BeginBatch(); // This batch will start with only the white texture in slot 0

                    // The new texture must now fit in the new batch, and it will be assigned to slot 1
                    textureSlot = (float)s_Data->TextureSlotIndex;
                    s_Data->TextureSlots[s_Data->TextureSlotIndex] = textureID;
                    s_Data->TextureSlotIndex++;
                } else {
                    // Assign to the next available slot
                    textureSlot = (float)s_Data->TextureSlotIndex;
                    s_Data->TextureSlots[s_Data->TextureSlotIndex] = textureID;
                    s_Data->TextureSlotIndex++;
                }
            }
        }

        // 3. Transform and Write 4 Vertices
        // Combine transforms: Scale * Rotate * Translate (to move the quad to world space)
        // Order of application is Scale, then Rotate (around origin), then Translate.
        Mat4 transform = Mat4::Translate(position) * Mat4::Rotate(rotation) * Mat4::Scale(size);

        // ERROR FIX 5: Change constexpr to const (or just define locally)
        const Vec2 quadPositions[4] = {
            {-0.5f, -0.5f}, // V0: Bottom-left
            { 0.5f, -0.5f}, // V1: Bottom-right
            { 0.5f,  0.5f}, // V2: Top-right
            {-0.5f,  0.5f}  // V3: Top-left
        };

        // ERROR FIX 5: Change constexpr to const
        const Vec2 texCoords[4] = {
            {0.0f, 0.0f}, // V0
            {1.0f, 0.0f}, // V1
            {1.0f, 1.0f}, // V2
            {0.0f, 1.0f}  // V3
        };

        for (int i = 0; i < 4; ++i) {
            // CRITICAL FIX 1 (Resolved): The call site now works because the operator returns Vec4.
            Vec4 transformedPos = transform * Vec4(quadPositions[i].x, quadPositions[i].y, 0.0f, 1.0f);

            s_Data->VertexBufferPtr->Position.x = transformedPos.x;
            s_Data->VertexBufferPtr->Position.y = transformedPos.y;
            s_Data->VertexBufferPtr->Color = color;
            s_Data->VertexBufferPtr->TexCoord = texCoords[i];
            s_Data->VertexBufferPtr->TexID = textureSlot;
            s_Data->VertexBufferPtr++;
        }

        // 4. Update index count (4 vertices added, 6 indices used)
        s_Data->IndexCount += 6;
    }

    // --- Camera Management Implementation ---

    void Renderer::SetCamera(const Vec2& position, float zoom, float halfHeight, float rotationRadians) {
        if (s_Data == nullptr) return;

        const float nearPlane = -1.0f;
        const float farPlane = 1.0f;

        // 1. Calculate Projection Matrix (Orthographic)
        // The viewport height is 2 * halfHeight.
        const float aspect = s_Data->ViewportWidth / s_Data->ViewportHeight;
        const float top = halfHeight / zoom;
        const float bottom = -top;
        const float right = top * aspect;
        const float left = -right;

        // CRITICAL FIX 5: Change Mat4::Ortho to Mat4::Orthographic to match Math.h
        // Resolves: error: 'Ortho' is not a member of 'Engine::Mat4'
        Mat4 projection = Mat4::Orthographic(left, right, bottom, top, nearPlane, farPlane);

        // 2. Calculate View Matrix (Rotation and Translation)
        // View matrix is the inverse of the camera's world transform.
        // We use -position for translation to move the world opposite the camera.
        Mat4 view = Mat4::Rotate(rotationRadians) * Mat4::Translate({-position.x, -position.y});

        // 3. Final View-Projection Matrix
        Mat4 viewProjection = projection * view;

        // 4. Send to Shader
        glUseProgram(s_Data->ShaderID);
        // Find the uniform location and set the matrix data
        const int vpUniform = glGetUniformLocation(s_Data->ShaderID, "u_ViewProjection");
        // GL_TRUE means the matrix is row-major, Mat4::elements is column-major so we pass GL_FALSE to match standard GLM/OpenGL.
        glUniformMatrix4fv(vpUniform, 1, GL_FALSE, viewProjection.elements);
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
        unsigned char *data = stbi_load(filePath.c_str(), &width, &height, &nrChannels, 0);

        if (data) {
            GLenum format = GL_RGB;
            if (nrChannels == 4) format = GL_RGBA;
            else if (nrChannels == 1) format = GL_RED;

            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            stbi_image_free(data);
            return textureID;
        } else {
            std::cerr << "Texture failed to load at path: " << filePath << std::endl;
            stbi_image_free(data);
            return 0;
        }
    }

} // namespace Engine