#include "../include/TextureCutterUI.h"
#include "../include/EditorUi.h"

#include <algorithm>

#include "imgui.h"
#include <string>
#include <vector>

#include "TextureManager.h"
#include "SpriteMetaFile.h"
#include "EngineDefines.h"

using namespace EditorWindows;

namespace {
    // Local sprite data for editing (mirrors SpriteRegion but with editing state)
    struct EditableSpriteRegion {
        std::string name;
        int x, y, width, height; // Pixel coordinates (top-left origin for editor)
        float pivotX = 0.5f, pivotY = 0.5f; // Normalized pivot (0-1)
        bool isSelected = false;
    };

    enum class InteractionMode {
        None,
        Selecting,
        Moving,
        ResizingTopLeft,
        ResizingTopRight,
        ResizingBottomLeft,
        ResizingBottomRight,
        ResizingTop,
        ResizingBottom,
        ResizingLeft,
        ResizingRight,
        MovingPivot
    };

    struct TextureCutterState {
        std::string currentTexturePath;
        Engine::TextureInfo currentTexture;
        Engine::SpriteMetaFile* metaFile = nullptr;
        bool isOpen = false;

        std::vector<EditableSpriteRegion> sprites;
        int selectedSpriteIndex = -1;

        ImVec2 selectionStart = {0, 0};
        ImVec2 selectionEnd = {0, 0};
        InteractionMode currentMode = InteractionMode::None;

        ImVec2 dragStartPos = {0, 0};
        int originalX = 0, originalY = 0, originalWidth = 0, originalHeight = 0;
        float originalPivotX = 0.5f, originalPivotY = 0.5f;

        char spriteNameBuffer[128] = "";
        bool showNamePopup = false;

        char editNameBuffer[128] = "";
        int editX = 0, editY = 0, editWidth = 0, editHeight = 0;
        float editPivotX = 0.5f, editPivotY = 0.5f;

        float zoomLevel = 1.0f;
        ImVec2 panOffset = {0, 0};
    };

    static TextureCutterState s_State;

    void LoadSpritesFromMeta() {
        s_State.sprites.clear();

        if (!s_State.metaFile) return;

        for (const auto& sprite : s_State.metaFile->sprites) {
            EditableSpriteRegion region;
            region.name = sprite.name;
            region.x = sprite.pixelX;
            region.y = sprite.pixelY;
            region.width = sprite.pixelWidth;
            region.height = sprite.pixelHeight;
            region.pivotX = sprite.pivot.x;
            region.pivotY = sprite.pivot.y;
            s_State.sprites.push_back(region);
        }
    }

    void SaveCurrentSpriteToMeta() {
        if (!s_State.metaFile || s_State.selectedSpriteIndex < 0) return;
        if (s_State.selectedSpriteIndex >= static_cast<int>(s_State.sprites.size())) return;

        const auto& editSprite = s_State.sprites[s_State.selectedSpriteIndex];

        Engine::SpriteRegion* metaSprite = s_State.metaFile->FindSprite(editSprite.name);
        if (!metaSprite) return;

        // Update UV from pixel coordinates
        metaSprite->uvRect = s_State.metaFile->PixelToUV(
            editSprite.x, editSprite.y, editSprite.width, editSprite.height);
        metaSprite->pixelX = editSprite.x;
        metaSprite->pixelY = editSprite.y;
        metaSprite->pixelWidth = editSprite.width;
        metaSprite->pixelHeight = editSprite.height;
        metaSprite->pivot.x = editSprite.pivotX;
        metaSprite->pivot.y = editSprite.pivotY;

        s_State.metaFile->Save();

        // Update TextureManager
        Engine::TextureManager::ReloadMetaFile(s_State.currentTexturePath);
    }

    Engine::Math::Vec4 EditorCoordsToUV(int x, int y, int width, int height) {
        if (!s_State.metaFile) return {0, 0, 1, 1};
        return s_State.metaFile->PixelToUV(x, y, width, height);
    }

    const float EDGE_THRESHOLD = 8.0f;
    const float PIVOT_HANDLE_RADIUS = 8.0f;

    InteractionMode GetInteractionMode(const ImVec2& mouseTexCoords, int spriteIndex) {
        if (spriteIndex < 0 || spriteIndex >= static_cast<int>(s_State.sprites.size())) {
            return InteractionMode::None;
        }

        const auto& sprite = s_State.sprites[spriteIndex];
        const float threshold = EDGE_THRESHOLD / s_State.zoomLevel;

        const float mx = mouseTexCoords.x;
        const float my = mouseTexCoords.y;

        // Check pivot handle first
        float pivotScreenX = sprite.x + sprite.width * sprite.pivotX;
        float pivotScreenY = sprite.y + sprite.height * (1.0f - sprite.pivotY); // Flip Y for screen
        float pivotDist = Engine::Math::Sqrt((mx - pivotScreenX) * (mx - pivotScreenX) +
                                    (my - pivotScreenY) * (my - pivotScreenY));
        if (pivotDist < PIVOT_HANDLE_RADIUS / s_State.zoomLevel) {
            return InteractionMode::MovingPivot;
        }

        const bool nearLeft = std::abs(mx - sprite.x) < threshold;
        const bool nearRight = std::abs(mx - (sprite.x + sprite.width)) < threshold;
        const bool nearTop = std::abs(my - sprite.y) < threshold;
        const bool nearBottom = std::abs(my - (sprite.y + sprite.height)) < threshold;

        const bool insideX = mx >= sprite.x && mx <= sprite.x + sprite.width;
        const bool insideY = my >= sprite.y && my <= sprite.y + sprite.height;

        if (nearTop && nearLeft) return InteractionMode::ResizingTopLeft;
        if (nearTop && nearRight) return InteractionMode::ResizingTopRight;
        if (nearBottom && nearLeft) return InteractionMode::ResizingBottomLeft;
        if (nearBottom && nearRight) return InteractionMode::ResizingBottomRight;

        if (nearTop && insideX) return InteractionMode::ResizingTop;
        if (nearBottom && insideX) return InteractionMode::ResizingBottom;
        if (nearLeft && insideY) return InteractionMode::ResizingLeft;
        if (nearRight && insideY) return InteractionMode::ResizingRight;

        if (insideX && insideY) return InteractionMode::Moving;

        return InteractionMode::None;
    }

    ImGuiMouseCursor GetCursorForMode(InteractionMode mode) {
        switch (mode) {
            case InteractionMode::Moving: return ImGuiMouseCursor_Hand;
            case InteractionMode::MovingPivot: return ImGuiMouseCursor_Hand;
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

bool TextureCutterUI::isOpen = false;

void TextureCutterUI::OpenTextureCutter(const std::string& texturePath) {
    s_State.currentTexturePath = texturePath;

    // Ensure meta file exists and load it
    s_State.metaFile = Engine::TextureManager::GetOrCreateMetaFile(texturePath);

    // Load texture (may lazy-load)
    std::string defaultSpriteName = s_State.metaFile && !s_State.metaFile->sprites.empty()
        ? s_State.metaFile->sprites[0].name
        : Engine::TextureManager::GetFileNameWithoutExtension(texturePath);

    s_State.currentTexture = Engine::TextureManager::GetTexture(defaultSpriteName);

    if (s_State.currentTexture.ID == 0) {
        Engine::TextureManager::LoadTexture(texturePath);
        s_State.currentTexture = Engine::TextureManager::GetTexture(defaultSpriteName);
    }

    s_State.isOpen = true;
    isOpen = true;
    EditorUi::showTextureCutter = true;  // Sync with View menu
    s_State.zoomLevel = -1.0f;
    s_State.panOffset = {0, 0};
    s_State.currentMode = InteractionMode::None;
    s_State.selectedSpriteIndex = -1;

    LoadSpritesFromMeta();
}

void TextureCutterUI::TextureCutterWindow() {
    // Sync with EditorUi visibility flag
    if (!s_State.isOpen && !s_State.currentTexturePath.empty()) {
        // Window was closed, sync visibility flag
        EditorUi::showTextureCutter = false;
    }

    if (!s_State.isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Texture Cutter", &s_State.isOpen)) {
        ImGui::End();
        isOpen = s_State.isOpen;
        // Sync with EditorUi visibility flag
        if (!s_State.isOpen) EditorUi::showTextureCutter = false;
        return;
    }

    if (s_State.currentTexture.ID == 0) {
        ImGui::Text("Failed to load texture: %s", s_State.currentTexturePath.c_str());
        ImGui::End();
        return;
    }

    const float leftPanelWidth = 250.0f;
    const float rightPanelWidth = 300.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float canvasWidth = availableWidth - leftPanelWidth - rightPanelWidth - spacing * 2;

    // ========== LEFT PANEL: Sprite List ==========
    ImGui::BeginChild("SpriteList", ImVec2(leftPanelWidth, 0), true);
    ImGui::Text("Sprites (%zu)", s_State.sprites.size());
    ImGui::Separator();

    for (int i = 0; i < static_cast<int>(s_State.sprites.size()); i++) {
        const bool isSelected = (s_State.selectedSpriteIndex == i);

        if (ImGui::Selectable(s_State.sprites[i].name.c_str(), isSelected)) {
            s_State.selectedSpriteIndex = i;
            s_State.currentMode = InteractionMode::None;

            strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
            s_State.editX = s_State.sprites[i].x;
            s_State.editY = s_State.sprites[i].y;
            s_State.editWidth = s_State.sprites[i].width;
            s_State.editHeight = s_State.sprites[i].height;
            s_State.editPivotX = s_State.sprites[i].pivotX;
            s_State.editPivotY = s_State.sprites[i].pivotY;
        }

        if (isSelected) {
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "%dx%d at (%d,%d)",
                s_State.sprites[i].width,
                s_State.sprites[i].height,
                s_State.sprites[i].x,
                s_State.sprites[i].y);
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f),
                "Pivot: (%.2f, %.2f)",
                s_State.sprites[i].pivotX,
                s_State.sprites[i].pivotY);
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

    ImGui::BeginChild("CanvasArea", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(50, 50, 50, 255));

    if (s_State.zoomLevel < 0.0f) {
        const float border = 40.0f;
        const float availWidth = canvasSize.x - border * 2;
        const float availHeight = canvasSize.y - border * 2;

        const float scaleX = availWidth / s_State.currentTexture.Width;
        const float scaleY = availHeight / s_State.currentTexture.Height;

        s_State.zoomLevel = std::min(scaleX, scaleY);
        s_State.panOffset = {0, 0};
    }

    const float texWidth = s_State.currentTexture.Width * s_State.zoomLevel;
    const float texHeight = s_State.currentTexture.Height * s_State.zoomLevel;

    const ImVec2 texPos = ImVec2(
        canvasPos.x + (canvasSize.x - texWidth) * 0.5f + s_State.panOffset.x,
        canvasPos.y + (canvasSize.y - texHeight) * 0.5f + s_State.panOffset.y
    );

    // Draw texture (flipped for ImGui)
    drawList->AddImage(
        (void*)static_cast<intptr_t>(s_State.currentTexture.ID),
        texPos,
        ImVec2(texPos.x + texWidth, texPos.y + texHeight),
        ImVec2(0, 1), ImVec2(1, 0)
    );

    // Draw all sprites
    for (int i = 0; i < static_cast<int>(s_State.sprites.size()); i++) {
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

        // Draw pivot point
        float pivotScreenX = texPos.x + (sprite.x + sprite.width * sprite.pivotX) * s_State.zoomLevel;
        float pivotScreenY = texPos.y + (sprite.y + sprite.height * (1.0f - sprite.pivotY)) * s_State.zoomLevel;
        ImU32 pivotColor = isSelected ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 100, 100, 150);
        drawList->AddCircleFilled(ImVec2(pivotScreenX, pivotScreenY), 5.0f, pivotColor);
        drawList->AddCircle(ImVec2(pivotScreenX, pivotScreenY), 5.0f, IM_COL32(255, 255, 255, 200), 12, 1.5f);

        // Draw crosshair at pivot
        if (isSelected) {
            float crossSize = 12.0f;
            drawList->AddLine(
                ImVec2(pivotScreenX - crossSize, pivotScreenY),
                ImVec2(pivotScreenX + crossSize, pivotScreenY),
                IM_COL32(255, 100, 100, 200), 1.5f);
            drawList->AddLine(
                ImVec2(pivotScreenX, pivotScreenY - crossSize),
                ImVec2(pivotScreenX, pivotScreenY + crossSize),
                IM_COL32(255, 100, 100, 200), 1.5f);
        }

        // Draw resize handles for selected
        if (isSelected && s_State.zoomLevel > 0.5f) {
            const float handleSize = 6.0f;
            const ImU32 handleColor = IM_COL32(255, 255, 0, 255);

            drawList->AddRectFilled(
                ImVec2(rectMin.x - handleSize/2, rectMin.y - handleSize/2),
                ImVec2(rectMin.x + handleSize/2, rectMin.y + handleSize/2), handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMax.x - handleSize/2, rectMin.y - handleSize/2),
                ImVec2(rectMax.x + handleSize/2, rectMin.y + handleSize/2), handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMin.x - handleSize/2, rectMax.y - handleSize/2),
                ImVec2(rectMin.x + handleSize/2, rectMax.y + handleSize/2), handleColor);
            drawList->AddRectFilled(
                ImVec2(rectMax.x - handleSize/2, rectMax.y - handleSize/2),
                ImVec2(rectMax.x + handleSize/2, rectMax.y + handleSize/2), handleColor);
        }

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

    // Update cursor
    if (!anyPopupOpen && isHovered && s_State.currentMode == InteractionMode::None) {
        InteractionMode hoverMode = GetInteractionMode(mouseTexCoords, s_State.selectedSpriteIndex);
        ImGui::SetMouseCursor(GetCursorForMode(hoverMode));
    }

    // Handle left mouse button
    if (!anyPopupOpen && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (mouseTexCoords.x >= 0 && mouseTexCoords.x <= s_State.currentTexture.Width &&
            mouseTexCoords.y >= 0 && mouseTexCoords.y <= s_State.currentTexture.Height) {

            InteractionMode mode = GetInteractionMode(mouseTexCoords, s_State.selectedSpriteIndex);

            if (mode != InteractionMode::None) {
                s_State.currentMode = mode;
                s_State.dragStartPos = mouseTexCoords;

                if (s_State.selectedSpriteIndex >= 0) {
                    s_State.originalX = s_State.sprites[s_State.selectedSpriteIndex].x;
                    s_State.originalY = s_State.sprites[s_State.selectedSpriteIndex].y;
                    s_State.originalWidth = s_State.sprites[s_State.selectedSpriteIndex].width;
                    s_State.originalHeight = s_State.sprites[s_State.selectedSpriteIndex].height;
                    s_State.originalPivotX = s_State.sprites[s_State.selectedSpriteIndex].pivotX;
                    s_State.originalPivotY = s_State.sprites[s_State.selectedSpriteIndex].pivotY;
                }
            } else {
                s_State.currentMode = InteractionMode::Selecting;
                s_State.selectionStart = mouseTexCoords;
                s_State.selectionEnd = mouseTexCoords;

                bool clickedSprite = false;
                for (int i = 0; i < static_cast<int>(s_State.sprites.size()); i++) {
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
                        s_State.editPivotX = s_State.sprites[i].pivotX;
                        s_State.editPivotY = s_State.sprites[i].pivotY;
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

    // Handle dragging
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
        } else if (s_State.currentMode == InteractionMode::MovingPivot && s_State.selectedSpriteIndex >= 0) {
            auto& sprite = s_State.sprites[s_State.selectedSpriteIndex];
            // Calculate new pivot in normalized coordinates
            float relX = (mouseTexCoords.x - sprite.x) / sprite.width;
            float relY = 1.0f - (mouseTexCoords.y - sprite.y) / sprite.height; // Flip Y
            sprite.pivotX = std::clamp(relX, 0.0f, 1.0f);
            sprite.pivotY = std::clamp(relY, 0.0f, 1.0f);
            s_State.editPivotX = sprite.pivotX;
            s_State.editPivotY = sprite.pivotY;
        } else if (s_State.selectedSpriteIndex >= 0) {
            auto& sprite = s_State.sprites[s_State.selectedSpriteIndex];

            switch (s_State.currentMode) {
                case InteractionMode::Moving:
                    sprite.x = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.currentTexture.Width - sprite.width);
                    sprite.y = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.currentTexture.Height - sprite.height);
                    break;

                case InteractionMode::ResizingTopLeft: {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);
                    sprite.width = s_State.originalWidth - (newX - s_State.originalX);
                    sprite.height = s_State.originalHeight - (newY - s_State.originalY);
                    sprite.x = newX;
                    sprite.y = newY;
                } break;

                case InteractionMode::ResizingTopRight: {
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);
                    int newWidth = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);
                    sprite.width = newWidth;
                    sprite.height = s_State.originalHeight - (newY - s_State.originalY);
                    sprite.y = newY;
                } break;

                case InteractionMode::ResizingBottomLeft: {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    int newHeight = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);
                    sprite.width = s_State.originalWidth - (newX - s_State.originalX);
                    sprite.height = newHeight;
                    sprite.x = newX;
                } break;

                case InteractionMode::ResizingBottomRight:
                    sprite.width = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);
                    sprite.height = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);
                    break;

                case InteractionMode::ResizingTop: {
                    int newY = std::clamp(s_State.originalY + static_cast<int>(delta.y), 0, s_State.originalY + s_State.originalHeight - 1);
                    sprite.y = newY;
                    sprite.height = (s_State.originalY + s_State.originalHeight) - newY;
                } break;

                case InteractionMode::ResizingBottom:
                    sprite.height = std::clamp(s_State.originalHeight + static_cast<int>(delta.y), 1, s_State.currentTexture.Height - s_State.originalY);
                    break;

                case InteractionMode::ResizingLeft: {
                    int newX = std::clamp(s_State.originalX + static_cast<int>(delta.x), 0, s_State.originalX + s_State.originalWidth - 1);
                    sprite.x = newX;
                    sprite.width = (s_State.originalX + s_State.originalWidth) - newX;
                } break;

                case InteractionMode::ResizingRight:
                    sprite.width = std::clamp(s_State.originalWidth + static_cast<int>(delta.x), 1, s_State.currentTexture.Width - s_State.originalX);
                    break;

                default: break;
            }

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
        } else if (s_State.selectedSpriteIndex >= 0) {
            SaveCurrentSpriteToMeta();
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

    ImGui::EndChild(); // CanvasArea
    ImGui::EndChild(); // CanvasColumn

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
        ImGui::Text("Pivot");
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(0,0)=Bottom-Left, (1,1)=Top-Right");

        if (ImGui::DragFloat("Pivot X", &s_State.editPivotX, 0.01f, 0.0f, 1.0f, "%.2f")) {
            s_State.sprites[s_State.selectedSpriteIndex].pivotX = s_State.editPivotX;
        }
        if (ImGui::DragFloat("Pivot Y", &s_State.editPivotY, 0.01f, 0.0f, 1.0f, "%.2f")) {
            s_State.sprites[s_State.selectedSpriteIndex].pivotY = s_State.editPivotY;
        }

        // Quick pivot presets
        ImGui::Text("Presets:");
        if (ImGui::Button("Center")) { s_State.editPivotX = 0.5f; s_State.editPivotY = 0.5f; }
        ImGui::SameLine();
        if (ImGui::Button("Bottom")) { s_State.editPivotX = 0.5f; s_State.editPivotY = 0.0f; }
        ImGui::SameLine();
        if (ImGui::Button("Top")) { s_State.editPivotX = 0.5f; s_State.editPivotY = 1.0f; }

        if (ImGui::Button("BL")) { s_State.editPivotX = 0.0f; s_State.editPivotY = 0.0f; }
        ImGui::SameLine();
        if (ImGui::Button("BR")) { s_State.editPivotX = 1.0f; s_State.editPivotY = 0.0f; }
        ImGui::SameLine();
        if (ImGui::Button("TL")) { s_State.editPivotX = 0.0f; s_State.editPivotY = 1.0f; }
        ImGui::SameLine();
        if (ImGui::Button("TR")) { s_State.editPivotX = 1.0f; s_State.editPivotY = 1.0f; }

        s_State.sprites[s_State.selectedSpriteIndex].pivotX = s_State.editPivotX;
        s_State.sprites[s_State.selectedSpriteIndex].pivotY = s_State.editPivotY;

        ImGui::Separator();

        if (ImGui::Button("Apply Changes", ImVec2(-1, 0))) {
            const std::string oldName = s_State.sprites[s_State.selectedSpriteIndex].name;
            const std::string newName = s_State.editNameBuffer;

            if (s_State.metaFile) {
                if (oldName != newName) {
                    // Rename in meta file
                    s_State.metaFile->RenameSprite(oldName, newName);
                    s_State.sprites[s_State.selectedSpriteIndex].name = newName;
                }

                SaveCurrentSpriteToMeta();
                LoadSpritesFromMeta();

                // Re-select
                for (int i = 0; i < static_cast<int>(s_State.sprites.size()); i++) {
                    if (s_State.sprites[i].name == newName) {
                        s_State.selectedSpriteIndex = i;
                        break;
                    }
                }
            }
        }

        if (ImGui::Button("Delete Sprite", ImVec2(-1, 0))) {
            if (s_State.metaFile) {
                s_State.metaFile->RemoveSprite(s_State.sprites[s_State.selectedSpriteIndex].name);
                s_State.metaFile->Save();
                Engine::TextureManager::ReloadMetaFile(s_State.currentTexturePath);
                LoadSpritesFromMeta();
                s_State.selectedSpriteIndex = -1;
            }
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
        ImGui::TextWrapped("Drag red pivot point to reposition.");
        ImGui::Spacing();
        ImGui::TextWrapped("Click and drag empty space to create new sprite.");
    }

    ImGui::EndChild();

    // ========== POPUP: New Sprite Name ==========
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
            if (s_State.metaFile) {
                Engine::Math::Vec4 uvRect = s_State.metaFile->PixelToUV(
                    selMinX, selMinY, selMaxX - selMinX, selMaxY - selMinY);

                if (s_State.metaFile->AddSprite(s_State.spriteNameBuffer, uvRect)) {
                    // Save first
                    if (!s_State.metaFile->Save()) {
                        LOG_ERROR("Failed to save meta file after adding sprite");
                    }

                    // Reload sprites from our local meta file instead of through TextureManager
                    // This avoids issues when texture isn't fully loaded
                    LoadSpritesFromMeta();

                    // Now safely update TextureManager (non-blocking)
                    try {
                        Engine::TextureManager::ReloadMetaFile(s_State.currentTexturePath);
                    } catch (...) {
                        LOG_WARN("Failed to reload meta file in TextureManager");
                    }

                    // Select the new sprite
                    std::string newSpriteName = s_State.spriteNameBuffer;
                    for (int i = 0; i < static_cast<int>(s_State.sprites.size()); i++) {
                        if (s_State.sprites[i].name == newSpriteName) {
                            s_State.selectedSpriteIndex = i;
                            strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
                            s_State.editX = s_State.sprites[i].x;
                            s_State.editY = s_State.sprites[i].y;
                            s_State.editWidth = s_State.sprites[i].width;
                            s_State.editHeight = s_State.sprites[i].height;
                            s_State.editPivotX = s_State.sprites[i].pivotX;
                            s_State.editPivotY = s_State.sprites[i].pivotY;
                            break;
                        }
                    }

                    justOpened = true;
                    ImGui::CloseCurrentPopup();
                }
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
    isOpen = s_State.isOpen;

    // Sync visibility flag when window is closed
    if (!s_State.isOpen) {
        EditorUi::showTextureCutter = false;
    }
}
