#include "../include/TextureCutterUI.h"

#include <algorithm>

#include "imgui.h"
#include <string>
#include <vector>

#include "TextureManager.h"

using namespace EditorWindows;

namespace {
    // Represents a sprite region
    struct SpriteRegion {
        std::string name;
        int x, y, width, height; // Pixel coordinates
        bool isSelected = false;
    };

    // Interaction modes for canvas manipulation
    enum class InteractionMode {
        None,
        Selecting,      // Creating new sprite region
        Moving,         // Moving entire sprite
        ResizingTopLeft,
        ResizingTopRight,
        ResizingBottomLeft,
        ResizingBottomRight,
        ResizingTop,
        ResizingBottom,
        ResizingLeft,
        ResizingRight
    };

    // State for the texture cutter window
    struct TextureCutterState {
        std::string currentTexturePath;
        Engine::TextureInfo currentTexture;
        bool isOpen = false;

        // All sprites from this texture
        std::vector<SpriteRegion> sprites;
        int selectedSpriteIndex = -1;

        // Selection state for creating new sprites
        ImVec2 selectionStart = {0, 0};
        ImVec2 selectionEnd = {0, 0};
        InteractionMode currentMode = InteractionMode::None;

        // For moving/resizing
        ImVec2 dragStartPos = {0, 0};
        int originalX = 0, originalY = 0, originalWidth = 0, originalHeight = 0;

        // UI state
        char spriteNameBuffer[128] = "";
        bool showNamePopup = false;

        // Edit buffers for selected sprite
        char editNameBuffer[128] = "";
        int editX = 0, editY = 0, editWidth = 0, editHeight = 0;

        // Display settings
        float zoomLevel = 1.0f;
        ImVec2 panOffset = {0, 0};
    };

    static TextureCutterState s_State;

    // Helper to load all existing sprites for this texture
    void LoadExistingSprites() {
        s_State.sprites.clear();

        // Iterate through all loaded textures and find ones from this source
        for (const auto& [name, info] : Engine::TextureManager::GetAllTextures()) {
            if (info.SourceFilePath == s_State.currentTexturePath) {
                SpriteRegion region;
                region.name = name;

                // CRITICAL FIX: OpenGL has origin at bottom-left, we need to flip Y coordinate
                // UV coordinates are stored as (x, y, width, height) where y is from bottom
                // Convert to top-left origin for editing
                region.x = static_cast<int>(info.TextureUVRect.x * s_State.currentTexture.Width);
                region.width = static_cast<int>(info.TextureUVRect.z * s_State.currentTexture.Width);
                region.height = static_cast<int>(info.TextureUVRect.w * s_State.currentTexture.Height);

                // Flip Y: convert from bottom-origin to top-origin
                float bottomY = info.TextureUVRect.y;
                float topY = bottomY + info.TextureUVRect.w;
                region.y = static_cast<int>((1.0f - topY) * s_State.currentTexture.Height);

                s_State.sprites.push_back(region);
            }
        }
    }

    // Helper to convert editor coordinates (top-left origin) to OpenGL UV (bottom-left origin)
    Engine::Math::Vec4 EditorCoordsToUV(int x, int y, int width, int height) {
        const float texWidth = static_cast<float>(s_State.currentTexture.Width);
        const float texHeight = static_cast<float>(s_State.currentTexture.Height);

        // Convert to normalized coordinates (0-1 range)
        const float uvX = static_cast<float>(x) / texWidth;
        const float uvWidth = static_cast<float>(width) / texWidth;
        const float uvHeight = static_cast<float>(height) / texHeight;

        // CRITICAL: Flip Y coordinate from top-origin to bottom-origin
        // Editor Y is from top (0 at top, increases downward)
        // OpenGL Y is from bottom (0 at bottom, increases upward)
        const float topY = static_cast<float>(y) / texHeight;
        const float uvY = 1.0f - topY - uvHeight;

        return Engine::Math::Vec4(uvX, uvY, uvWidth, uvHeight);
    }

    // Helper to check if point is near a sprite edge (for resize handles)
    const float EDGE_THRESHOLD = 8.0f; // pixels

    InteractionMode GetInteractionMode(const ImVec2& mouseTexCoords, int spriteIndex) {
        if (spriteIndex < 0 || spriteIndex >= s_State.sprites.size()) {
            return InteractionMode::None;
        }

        const auto& sprite = s_State.sprites[spriteIndex];
        const float threshold = EDGE_THRESHOLD / s_State.zoomLevel; // Adjust for zoom

        const float mx = mouseTexCoords.x;
        const float my = mouseTexCoords.y;

        const bool nearLeft = std::abs(mx - sprite.x) < threshold;
        const bool nearRight = std::abs(mx - (sprite.x + sprite.width)) < threshold;
        const bool nearTop = std::abs(my - sprite.y) < threshold;
        const bool nearBottom = std::abs(my - (sprite.y + sprite.height)) < threshold;

        const bool insideX = mx >= sprite.x && mx <= sprite.x + sprite.width;
        const bool insideY = my >= sprite.y && my <= sprite.y + sprite.height;

        // Check corners first (they take priority)
        if (nearTop && nearLeft) return InteractionMode::ResizingTopLeft;
        if (nearTop && nearRight) return InteractionMode::ResizingTopRight;
        if (nearBottom && nearLeft) return InteractionMode::ResizingBottomLeft;
        if (nearBottom && nearRight) return InteractionMode::ResizingBottomRight;

        // Then check edges
        if (nearTop && insideX) return InteractionMode::ResizingTop;
        if (nearBottom && insideX) return InteractionMode::ResizingBottom;
        if (nearLeft && insideY) return InteractionMode::ResizingLeft;
        if (nearRight && insideY) return InteractionMode::ResizingRight;

        // If inside the sprite but not near edges, it's a move
        if (insideX && insideY) return InteractionMode::Moving;

        return InteractionMode::None;
    }

    // Helper to get appropriate mouse cursor for interaction mode
    ImGuiMouseCursor GetCursorForMode(InteractionMode mode) {
        switch (mode) {
            case InteractionMode::Moving: return ImGuiMouseCursor_Hand;
            case InteractionMode::ResizingTopLeft:
            case InteractionMode::ResizingBottomRight: return ImGuiMouseCursor_ResizeNWSE;
            case InteractionMode::ResizingTopRight:
            case InteractionMode::ResizingBottomLeft: return ImGuiMouseCursor_ResizeNESW;
            case InteractionMode::ResizingTop:
            case InteractionMode::ResizingBottom: return ImGuiMouseCursor_ResizeNS;
            case InteractionMode::ResizingLeft:
            case InteractionMode::ResizingRight: return ImGuiMouseCursor_ResizeEW;
            default: return ImGuiMouseCursor_Arrow;
        }
    }
}

void TextureCutterUI::OpenTextureCutter(const std::string& texturePath) {
    s_State.currentTexturePath = texturePath;
    s_State.currentTexture = Engine::TextureManager::GetTexture(
        Engine::TextureManager::GetFileNameWithoutExtension(texturePath)
    );

    if (s_State.currentTexture.ID == 0) {
        Engine::TextureManager::LoadTexture(texturePath);
        s_State.currentTexture = Engine::TextureManager::GetTexture(
            Engine::TextureManager::GetFileNameWithoutExtension(texturePath)
        );
    }

    s_State.isOpen = true;
    s_State.zoomLevel = -1.0f; // -1 signals "auto-fit on first render"
    s_State.panOffset = {0, 0};
    s_State.currentMode = InteractionMode::None;
    s_State.selectedSpriteIndex = -1;

    LoadExistingSprites();
}

void TextureCutterUI::TextureCutterWindow() {
    if (!s_State.isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Cutter", &s_State.isOpen)) {
        ImGui::End();
        return;
    }

    if (s_State.currentTexture.ID == 0) {
        ImGui::Text("Failed to load texture: %s", s_State.currentTexturePath.c_str());
        ImGui::End();
        return;
    }

    // FIXED: Use BeginChild with calculated sizes instead of columns for better control
    const float leftPanelWidth = 250.0f;
    const float rightPanelWidth = 300.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float canvasWidth = availableWidth - leftPanelWidth - rightPanelWidth - spacing * 2;

    // ========== LEFT PANEL: Sprite List ==========
    ImGui::BeginChild("SpriteList", ImVec2(leftPanelWidth, 0), true);
    ImGui::Text("Sprites (%zu)", s_State.sprites.size());
    ImGui::Separator();

    for (int i = 0; i < s_State.sprites.size(); i++) {
        const bool isSelected = (s_State.selectedSpriteIndex == i);

        if (ImGui::Selectable(s_State.sprites[i].name.c_str(), isSelected)) {
            s_State.selectedSpriteIndex = i;
            s_State.currentMode = InteractionMode::None;

            // Load into edit buffers
            strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
            s_State.editX = s_State.sprites[i].x;
            s_State.editY = s_State.sprites[i].y;
            s_State.editWidth = s_State.sprites[i].width;
            s_State.editHeight = s_State.sprites[i].height;
        }

        // Show dimensions
        if (isSelected) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "%dx%d at (%d,%d)",
                s_State.sprites[i].width,
                s_State.sprites[i].height,
                s_State.sprites[i].x,
                s_State.sprites[i].y);
            ImGui::Unindent();
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // ========== CENTER PANEL: Canvas ==========
    ImGui::BeginChild("CanvasColumn", ImVec2(canvasWidth, 0), false);

    ImGui::Text("Texture: %s", s_State.currentTexturePath.c_str());
    ImGui::SameLine();
    ImGui::Text("| Size: %dx%d", s_State.currentTexture.Width, s_State.currentTexture.Height);

    // Zoom controls
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    if (ImGui::Button("-")) s_State.zoomLevel = std::max(0.1f, s_State.zoomLevel - 0.1f);
    ImGui::SameLine();
    ImGui::Text("%.1fx", s_State.zoomLevel);
    ImGui::SameLine();
    if (ImGui::Button("+")) s_State.zoomLevel = std::min(5.0f, s_State.zoomLevel + 0.1f);
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        s_State.zoomLevel = -1.0f;
        s_State.panOffset = {0, 0};
    }

    ImGui::Separator();

    // Canvas area
    ImGui::BeginChild("CanvasArea", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(50, 50, 50, 255));

    // Auto-fit on first render
    if (s_State.zoomLevel < 0.0f) {
        const float border = 40.0f;
        const float availWidth = canvasSize.x - border * 2;
        const float availHeight = canvasSize.y - border * 2;

        const float scaleX = availWidth / s_State.currentTexture.Width;
        const float scaleY = availHeight / s_State.currentTexture.Height;

        s_State.zoomLevel = std::min(scaleX, scaleY);
        s_State.panOffset = {0, 0};
    }

    // Calculate texture display
    const float texWidth = s_State.currentTexture.Width * s_State.zoomLevel;
    const float texHeight = s_State.currentTexture.Height * s_State.zoomLevel;

    const ImVec2 texPos = ImVec2(
        canvasPos.x + (canvasSize.x - texWidth) * 0.5f + s_State.panOffset.x,
        canvasPos.y + (canvasSize.y - texHeight) * 0.5f + s_State.panOffset.y
    );

    // Draw texture (FLIPPED for ImGui coordinate system)
    drawList->AddImage(
        (void*)static_cast<intptr_t>(s_State.currentTexture.ID),
        texPos,
        ImVec2(texPos.x + texWidth, texPos.y + texHeight),
        ImVec2(0, 1), ImVec2(1, 0)
    );

    // Draw all existing sprites
    for (int i = 0; i < s_State.sprites.size(); i++) {
        const auto& sprite = s_State.sprites[i];
        ImVec2 rectMin = ImVec2(
            texPos.x + sprite.x * s_State.zoomLevel,
            texPos.y + sprite.y * s_State.zoomLevel
        );
        ImVec2 rectMax = ImVec2(
            texPos.x + (sprite.x + sprite.width) * s_State.zoomLevel,
            texPos.y + (sprite.y + sprite.height) * s_State.zoomLevel
        );

        const bool isSelected = (i == s_State.selectedSpriteIndex);
        const ImU32 color = isSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 255, 255, 200);
        const float thickness = isSelected ? 3.0f : 2.0f;

        drawList->AddRect(rectMin, rectMax, color, 0.0f, 0, thickness);

        const ImU32 fillColor = isSelected ? IM_COL32(255, 255, 0, 30) : IM_COL32(0, 255, 255, 20);
        drawList->AddRectFilled(rectMin, rectMax, fillColor);

        // Draw resize handles for selected sprite
        if (isSelected && s_State.zoomLevel > 0.5f) {
            const float handleSize = 6.0f;
            const ImU32 handleColor = IM_COL32(255, 255, 0, 255);

            // Corner handles
            drawList->AddRectFilled(
                ImVec2(rectMin.x - handleSize/2, rectMin.y - handleSize/2),
                ImVec2(rectMin.x + handleSize/2, rectMin.y + handleSize/2),
                handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMax.x - handleSize/2, rectMin.y - handleSize/2),
                ImVec2(rectMax.x + handleSize/2, rectMin.y + handleSize/2),
                handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMin.x - handleSize/2, rectMax.y - handleSize/2),
                ImVec2(rectMin.x + handleSize/2, rectMax.y + handleSize/2),
                handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMax.x - handleSize/2, rectMax.y - handleSize/2),
                ImVec2(rectMax.x + handleSize/2, rectMax.y + handleSize/2),
                handleColor);
        }

        // Draw name label
        if (s_State.zoomLevel > 0.3f) {
            ImVec2 textPos = ImVec2(rectMin.x + 4, rectMin.y + 2);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), sprite.name.c_str());
        }
    }

    // Interaction handling
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const bool isHovered = ImGui::IsItemHovered();
    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // Convert mouse to texture coordinates
    const ImVec2 mouseTexCoords = ImVec2(
        (mousePos.x - texPos.x) / s_State.zoomLevel,
        (mousePos.y - texPos.y) / s_State.zoomLevel
    );

    // Right-click drag to pan
    if (!anyPopupOpen && isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        s_State.panOffset.x += delta.x;
        s_State.panOffset.y += delta.y;
    }

    // Update cursor based on hover state
    if (!anyPopupOpen && isHovered && s_State.currentMode == InteractionMode::None) {
        InteractionMode hoverMode = GetInteractionMode(mouseTexCoords, s_State.selectedSpriteIndex);
        ImGui::SetMouseCursor(GetCursorForMode(hoverMode));
    }

    // Handle left mouse button press
    if (!anyPopupOpen && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (mouseTexCoords.x >= 0 && mouseTexCoords.x <= s_State.currentTexture.Width &&
            mouseTexCoords.y >= 0 && mouseTexCoords.y <= s_State.currentTexture.Height) {

            // Check if we're interacting with selected sprite
            InteractionMode mode = GetInteractionMode(mouseTexCoords, s_State.selectedSpriteIndex);

            if (mode != InteractionMode::None) {
                // Start move/resize operation
                s_State.currentMode = mode;
                s_State.dragStartPos = mouseTexCoords;
                s_State.originalX = s_State.sprites[s_State.selectedSpriteIndex].x;
                s_State.originalY = s_State.sprites[s_State.selectedSpriteIndex].y;
                s_State.originalWidth = s_State.sprites[s_State.selectedSpriteIndex].width;
                s_State.originalHeight = s_State.sprites[s_State.selectedSpriteIndex].height;
            } else {
                // Start new selection
                s_State.currentMode = InteractionMode::Selecting;
                s_State.selectionStart = mouseTexCoords;
                s_State.selectionEnd = mouseTexCoords;

                // Check if we clicked on a different sprite
                bool clickedSprite = false;
                for (int i = 0; i < s_State.sprites.size(); i++) {
                    const auto& sprite = s_State.sprites[i];
                    if (mouseTexCoords.x >= sprite.x && mouseTexCoords.x <= sprite.x + sprite.width &&
                        mouseTexCoords.y >= sprite.y && mouseTexCoords.y <= sprite.y + sprite.height) {
                        s_State.selectedSpriteIndex = i;
                        clickedSprite = true;
                        strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
                        s_State.editX = s_State.sprites[i].x;
                        s_State.editY = s_State.sprites[i].y;
                        s_State.editWidth = s_State.sprites[i].width;
                        s_State.editHeight = s_State.sprites[i].height;
                        s_State.currentMode = InteractionMode::None;
                        break;
                        }
                }
                if (!clickedSprite) {
                    s_State.selectedSpriteIndex = -1;
                }
            }
            }
    }
    // Replace the entire dragging operations switch statement with this fixed version:

    if (!anyPopupOpen && s_State.currentMode != InteractionMode::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImVec2(
            mouseTexCoords.x - s_State.dragStartPos.x,
            mouseTexCoords.y - s_State.dragStartPos.y
        );

        if (s_State.currentMode == InteractionMode::Selecting) {
            s_State.selectionEnd = ImVec2(
                std::clamp(mouseTexCoords.x, 0.0f, static_cast<float>(s_State.currentTexture.Width)),
                std::clamp(mouseTexCoords.y, 0.0f, static_cast<float>(s_State.currentTexture.Height))
            );
        } else if (s_State.selectedSpriteIndex >= 0) {
            auto& sprite = s_State.sprites[s_State.selectedSpriteIndex];

            switch (s_State.currentMode) {
                case InteractionMode::Moving:
                    sprite.x = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.currentTexture.Width - sprite.width);
                    sprite.y = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.currentTexture.Height - sprite.height);
                    break;

                case InteractionMode::ResizingTopLeft:
                {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);

                    sprite.width = s_State.originalWidth - (newX - s_State.originalX);
                    sprite.height = s_State.originalHeight - (newY - s_State.originalY);
                    sprite.x = newX;
                    sprite.y = newY;
                }
                    break;

                case InteractionMode::ResizingTopRight:
                {
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);
                    int newWidth = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);

                    sprite.width = newWidth;
                    sprite.height = s_State.originalHeight - (newY - s_State.originalY);
                    sprite.y = newY;
                }
                    break;

                case InteractionMode::ResizingBottomLeft:
                {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    int newHeight = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);

                    sprite.width = s_State.originalWidth - (newX - s_State.originalX);
                    sprite.height = newHeight;
                    sprite.x = newX;
                }
                    break;

                case InteractionMode::ResizingBottomRight:
                    sprite.width = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);
                    sprite.height = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);
                    break;

                case InteractionMode::ResizingTop:
                {
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);
                    sprite.y = newY;
                    sprite.height = (s_State.originalY + s_State.originalHeight) - newY;
                }
                    break;

                case InteractionMode::ResizingBottom:
                {
                    int newHeight = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);
                    sprite.height = newHeight;
                }
                    break;

                case InteractionMode::ResizingLeft:
                {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    sprite.x = newX;
                    sprite.width = (s_State.originalX + s_State.originalWidth) - newX;
                }
                    break;

                case InteractionMode::ResizingRight:
                {
                    int newWidth = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);
                    sprite.width = newWidth;
                }
                    break;
            }

            // Update edit buffers in real-time
            s_State.editX = sprite.x;
            s_State.editY = sprite.y;
            s_State.editWidth = sprite.width;
            s_State.editHeight = sprite.height;
        }
    }

    // Handle mouse release
    if (s_State.currentMode != InteractionMode::None && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (s_State.currentMode == InteractionMode::Selecting) {
            const float width = std::abs(s_State.selectionEnd.x - s_State.selectionStart.x);
            const float height = std::abs(s_State.selectionEnd.y - s_State.selectionStart.y);

            if (width > 5.0f && height > 5.0f) {
                s_State.showNamePopup = true;
            }
        } else if (s_State.currentMode != InteractionMode::None && s_State.selectedSpriteIndex >= 0) {
            // FIXED: Update sprite instead of deleting and recreating
            const auto& sprite = s_State.sprites[s_State.selectedSpriteIndex];
            const Engine::Math::Vec4 uvRect = EditorCoordsToUV(sprite.x, sprite.y, sprite.width, sprite.height);

            Engine::TextureManager::UpdateSpriteUVRect(sprite.name, uvRect);
        }

        s_State.currentMode = InteractionMode::None;
    }

    // Draw current selection
    if (s_State.currentMode == InteractionMode::Selecting) {
        const float width = std::abs(s_State.selectionEnd.x - s_State.selectionStart.x);
        const float height = std::abs(s_State.selectionEnd.y - s_State.selectionStart.y);

        if (width > 2.0f || height > 2.0f) {
            const ImVec2 rectMin = ImVec2(
                texPos.x + std::min(s_State.selectionStart.x, s_State.selectionEnd.x) * s_State.zoomLevel,
                texPos.y + std::min(s_State.selectionStart.y, s_State.selectionEnd.y) * s_State.zoomLevel
            );
            const ImVec2 rectMax = ImVec2(
                texPos.x + std::max(s_State.selectionStart.x, s_State.selectionEnd.x) * s_State.zoomLevel,
                texPos.y + std::max(s_State.selectionStart.y, s_State.selectionEnd.y) * s_State.zoomLevel
            );

            drawList->AddRectFilled(rectMin, rectMax, IM_COL32(0, 255, 0, 50));
            drawList->AddRect(rectMin, rectMax, IM_COL32(0, 255, 0, 255), 0.0f, 0, 2.0f);
        }
    }

    ImGui::EndChild(); // End CanvasArea
    ImGui::EndChild(); // End CanvasColumn

    ImGui::SameLine();

    // ========== RIGHT PANEL: Properties ==========
    ImGui::BeginChild("Properties", ImVec2(rightPanelWidth, 0), true);

    if (s_State.selectedSpriteIndex >= 0) {
        ImGui::Text("Edit Sprite");
        ImGui::Separator();

        ImGui::InputText("Name", s_State.editNameBuffer, sizeof(s_State.editNameBuffer));

        ImGui::Separator();
        ImGui::Text("Position");
        if (ImGui::DragInt("X", &s_State.editX, 1.0f, 0, s_State.currentTexture.Width)) {
            s_State.sprites[s_State.selectedSpriteIndex].x = s_State.editX;
        }
        if (ImGui::DragInt("Y", &s_State.editY, 1.0f, 0, s_State.currentTexture.Height)) {
            s_State.sprites[s_State.selectedSpriteIndex].y = s_State.editY;
        }

        ImGui::Separator();
        ImGui::Text("Size");
        if (ImGui::DragInt("Width", &s_State.editWidth, 1.0f, 1, s_State.currentTexture.Width - s_State.editX)) {
            s_State.sprites[s_State.selectedSpriteIndex].width = s_State.editWidth;
        }
        if (ImGui::DragInt("Height", &s_State.editHeight, 1.0f, 1, s_State.currentTexture.Height - s_State.editY)) {
            s_State.sprites[s_State.selectedSpriteIndex].height = s_State.editHeight;
        }

        ImGui::Separator();

        if (ImGui::Button("Apply Changes", ImVec2(-1, 0))) {
            const std::string oldName = s_State.sprites[s_State.selectedSpriteIndex].name;
            const std::string newName = s_State.editNameBuffer;

            // Use the corrected UV conversion function
            const Engine::Math::Vec4 uvRect = EditorCoordsToUV(
                s_State.editX,
                s_State.editY,
                s_State.editWidth,
                s_State.editHeight
            );

            // FIXED: If name changed, we need to remove old and create new
            // Otherwise, just update the UV rect
            if (oldName != newName) {
                Engine::TextureManager::RemoveSprite(oldName);
                Engine::TextureManager::CreateSpriteFromTexture(
                    s_State.currentTexturePath,
                    newName,
                    uvRect
                );
            } else {
                Engine::TextureManager::UpdateSpriteUVRect(oldName, uvRect);
            }

            LoadExistingSprites();

            // Re-select the sprite with the new name
            for (int i = 0; i < s_State.sprites.size(); i++) {
                if (s_State.sprites[i].name == newName) {
                    s_State.selectedSpriteIndex = i;
                    break;
                }
            }
        }

        if (ImGui::Button("Delete Sprite", ImVec2(-1, 0))) {
            Engine::TextureManager::RemoveSprite(s_State.sprites[s_State.selectedSpriteIndex].name);
            LoadExistingSprites();
            s_State.selectedSpriteIndex = -1;
        }

    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No sprite selected");
        ImGui::Separator();
        ImGui::TextWrapped("Click to select a sprite.");
        ImGui::Spacing();
        ImGui::TextWrapped("Drag edges/corners to resize.");
        ImGui::Spacing();
        ImGui::TextWrapped("Drag center to move.");
        ImGui::Spacing();
        ImGui::TextWrapped("Click and drag empty space to create new sprite.");
    }

    ImGui::EndChild();

    // ========== POPUP: Name Entry for New Sprite ==========
    if (s_State.showNamePopup) {
        ImGui::OpenPopup("Sprite Name");
        s_State.showNamePopup = false;
        strcpy(s_State.spriteNameBuffer, "");
    }

    if (ImGui::BeginPopupModal("Sprite Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int selMinX = static_cast<int>(std::min(s_State.selectionStart.x, s_State.selectionEnd.x));
        const int selMinY = static_cast<int>(std::min(s_State.selectionStart.y, s_State.selectionEnd.y));
        const int selMaxX = static_cast<int>(std::max(s_State.selectionStart.x, s_State.selectionEnd.x));
        const int selMaxY = static_cast<int>(std::max(s_State.selectionStart.y, s_State.selectionEnd.y));

        ImGui::Text("Position: (%d, %d)", selMinX, selMinY);
        ImGui::Text("Size: %dx%d pixels", selMaxX - selMinX, selMaxY - selMinY);
        ImGui::Separator();

        ImGui::Text("Sprite Name:");

        static bool justOpened = true;
        if (justOpened) {
            ImGui::SetKeyboardFocusHere();
            justOpened = false;
        }

        const bool enterPressed = ImGui::InputText("##name", s_State.spriteNameBuffer,
            sizeof(s_State.spriteNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        const bool nameValid = strlen(s_State.spriteNameBuffer) > 0;

        if (!nameValid) {
            ImGui::BeginDisabled();
        }

        if ((ImGui::Button("Create Sprite") || enterPressed) && nameValid) {
            // Use the corrected UV conversion function
            const Engine::Math::Vec4 uvRect = EditorCoordsToUV(
                selMinX,
                selMinY,
                selMaxX - selMinX,
                selMaxY - selMinY
            );

            const bool success = Engine::TextureManager::CreateSpriteFromTexture(
                s_State.currentTexturePath,
                s_State.spriteNameBuffer,
                uvRect
            );

            if (success) {
                LoadExistingSprites();

                // Auto-select the newly created sprite
                for (int i = 0; i < s_State.sprites.size(); i++) {
                    if (s_State.sprites[i].name == s_State.spriteNameBuffer) {
                        s_State.selectedSpriteIndex = i;
                        strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
                        s_State.editX = s_State.sprites[i].x;
                        s_State.editY = s_State.sprites[i].y;
                        s_State.editWidth = s_State.sprites[i].width;
                        s_State.editHeight = s_State.sprites[i].height;
                        break;
                    }
                }

                justOpened = true;
                ImGui::CloseCurrentPopup();
            }
        }

        if (!nameValid) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            justOpened = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}