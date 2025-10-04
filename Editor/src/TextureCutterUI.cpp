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
        bool isDragging = false;

        // UI state
        char spriteNameBuffer[128] = "";
        bool showNamePopup = false;
        bool isEditingExisting = false;

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

                // Convert UV to pixels
                region.x = static_cast<int>(info.TextureUVRect.x * s_State.currentTexture.Width);
                region.y = static_cast<int>(info.TextureUVRect.y * s_State.currentTexture.Height);
                region.width = static_cast<int>(info.TextureUVRect.z * s_State.currentTexture.Width);
                region.height = static_cast<int>(info.TextureUVRect.w * s_State.currentTexture.Height);

                s_State.sprites.push_back(region);
            }
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
    s_State.isDragging = false;
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

    // Layout: Left sidebar (sprite list) + Main canvas + Right panel (properties)
    // Use persistent column widths that adjust to window size
    static float leftPanelWidth = 250.0f;
    static float rightPanelWidth = 300.0f;

    ImGui::Columns(3, "MainLayout", true);

    // Only set initial widths on first use
    static bool firstTime = true;
    if (firstTime) {
        ImGui::SetColumnWidth(0, leftPanelWidth);
        ImGui::SetColumnWidth(2, rightPanelWidth);
        firstTime = false;
    }

    // Store current widths for next frame
    leftPanelWidth = ImGui::GetColumnWidth(0);
    rightPanelWidth = ImGui::GetColumnWidth(2);

    // ========== LEFT PANEL: Sprite List ==========
    ImGui::BeginChild("SpriteList", ImVec2(0, 0), true);
    ImGui::Text("Sprites (%zu)", s_State.sprites.size());
    ImGui::Separator();

    for (int i = 0; i < s_State.sprites.size(); i++) {
        const bool isSelected = (s_State.selectedSpriteIndex == i);

        if (ImGui::Selectable(s_State.sprites[i].name.c_str(), isSelected)) {
            s_State.selectedSpriteIndex = i;
            s_State.isDragging = false;

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

    ImGui::NextColumn();

    // ========== CENTER PANEL: Canvas ==========
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
        s_State.zoomLevel = -1.0f; // Trigger auto-fit
        s_State.panOffset = {0, 0};
    }

    ImGui::Separator();

    // Canvas (wrapped in child window for proper clipping and scrolling)
    ImGui::BeginChild("CanvasArea", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(50, 50, 50, 255));

    // Auto-fit on first render (zoom level -1 signals this)
    if (s_State.zoomLevel < 0.0f) {
        const float border = 40.0f; // Padding around edges for UX
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
        ImVec2(0, 1), ImVec2(1, 0)  // Flipped UV coordinates
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

        // Draw outline
        drawList->AddRect(rectMin, rectMax, color, 0.0f, 0, thickness);

        // Draw semi-transparent fill
        const ImU32 fillColor = isSelected ? IM_COL32(255, 255, 0, 30) : IM_COL32(0, 255, 255, 20);
        drawList->AddRectFilled(rectMin, rectMax, fillColor);

        // Draw name label
        if (s_State.zoomLevel > 0.3f) {
            ImVec2 textPos = ImVec2(rectMin.x + 4, rectMin.y + 2);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), sprite.name.c_str());
        }
    }

    // Interaction - create invisible button for canvas input
    // This button will handle mouse clicks and drags on the canvas
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("canvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const bool isHovered = ImGui::IsItemHovered();
    const ImVec2 mousePos = ImGui::GetMousePos();

    // CRITICAL FIX: Check if ANY popup is currently open before processing canvas input
    // This prevents the canvas from stealing mouse events from popup buttons
    const bool anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // Right-click drag to pan - only process if no popup is open
    if (!anyPopupOpen && isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        s_State.panOffset.x += delta.x;
        s_State.panOffset.y += delta.y;
    }

    // Handle mouse interaction for sprite creation/selection - only if no popup is open
    if (!anyPopupOpen && isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Convert mouse position to texture coordinates and clamp
        float texX = (mousePos.x - texPos.x) / s_State.zoomLevel;
        float texY = (mousePos.y - texPos.y) / s_State.zoomLevel;

        // Check if click is within texture bounds
        if (texX >= 0 && texX <= s_State.currentTexture.Width &&
            texY >= 0 && texY <= s_State.currentTexture.Height) {

            // Start potential drag (will determine if it's a click or drag later)
            s_State.isDragging = true;
            s_State.selectionStart = ImVec2(texX, texY);
            s_State.selectionEnd = s_State.selectionStart;
        }
    }

    // Process dragging - only if no popup is open
    if (!anyPopupOpen && s_State.isDragging) {
        s_State.selectionEnd = ImVec2(
            (mousePos.x - texPos.x) / s_State.zoomLevel,
            (mousePos.y - texPos.y) / s_State.zoomLevel
        );

        s_State.selectionEnd.x = std::clamp(s_State.selectionEnd.x, 0.0f, static_cast<float>(s_State.currentTexture.Width));
        s_State.selectionEnd.y = std::clamp(s_State.selectionEnd.y, 0.0f, static_cast<float>(s_State.currentTexture.Height));

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            s_State.isDragging = false;

            const float width = std::abs(s_State.selectionEnd.x - s_State.selectionStart.x);
            const float height = std::abs(s_State.selectionEnd.y - s_State.selectionStart.y);

            // Distinguish between click and drag based on distance moved
            if (width < 5.0f && height < 5.0f) {
                // It was a click, not a drag - check if we clicked on a sprite
                bool clickedSprite = false;
                for (int i = 0; i < s_State.sprites.size(); i++) {
                    const auto& sprite = s_State.sprites[i];

                    if (s_State.selectionStart.x >= sprite.x &&
                        s_State.selectionStart.x <= sprite.x + sprite.width &&
                        s_State.selectionStart.y >= sprite.y &&
                        s_State.selectionStart.y <= sprite.y + sprite.height) {

                        s_State.selectedSpriteIndex = i;
                        clickedSprite = true;

                        // Load into edit buffers
                        strcpy(s_State.editNameBuffer, s_State.sprites[i].name.c_str());
                        s_State.editX = s_State.sprites[i].x;
                        s_State.editY = s_State.sprites[i].y;
                        s_State.editWidth = s_State.sprites[i].width;
                        s_State.editHeight = s_State.sprites[i].height;
                        break;
                    }
                }

                if (!clickedSprite) {
                    s_State.selectedSpriteIndex = -1;
                }
            }
            else if (width > 1.0f && height > 1.0f) {
                // It was a drag - prepare to create new sprite
                s_State.showNamePopup = true;
                s_State.isEditingExisting = false;
            }
        }
    }

    // Draw current selection (only if it's actually a drag, not just a click)
    if (s_State.isDragging) {
        const float width = std::abs(s_State.selectionEnd.x - s_State.selectionStart.x);
        const float height = std::abs(s_State.selectionEnd.y - s_State.selectionStart.y);

        // Only draw if drag distance is significant
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

    ImGui::NextColumn();

    // ========== RIGHT PANEL: Properties ==========
    ImGui::BeginChild("Properties", ImVec2(0, 0), true);

    if (s_State.selectedSpriteIndex >= 0) {
        ImGui::Text("Edit Sprite");
        ImGui::Separator();

        ImGui::InputText("Name", s_State.editNameBuffer, sizeof(s_State.editNameBuffer));

        ImGui::Separator();
        ImGui::Text("Position");
        ImGui::DragInt("X", &s_State.editX, 1.0f, 0, s_State.currentTexture.Width);
        ImGui::DragInt("Y", &s_State.editY, 1.0f, 0, s_State.currentTexture.Height);

        ImGui::Separator();
        ImGui::Text("Size");
        ImGui::DragInt("Width", &s_State.editWidth, 1.0f, 1, s_State.currentTexture.Width - s_State.editX);
        ImGui::DragInt("Height", &s_State.editHeight, 1.0f, 1, s_State.currentTexture.Height - s_State.editY);

        ImGui::Separator();

        if (ImGui::Button("Apply Changes", ImVec2(-1, 0))) {
            const std::string oldName = s_State.sprites[s_State.selectedSpriteIndex].name;

            const Engine::Math::Vec4 uvRect(
                static_cast<float>(s_State.editX) / s_State.currentTexture.Width,
                static_cast<float>(s_State.editY) / s_State.currentTexture.Height,
                static_cast<float>(s_State.editWidth) / s_State.currentTexture.Width,
                static_cast<float>(s_State.editHeight) / s_State.currentTexture.Height
            );

            Engine::TextureManager::RemoveSprite(oldName);
            Engine::TextureManager::CreateSpriteFromTexture(
                s_State.currentTexturePath,
                s_State.editNameBuffer,
                uvRect
            );

            LoadExistingSprites();
        }

        if (ImGui::Button("Delete Sprite", ImVec2(-1, 0))) {
            Engine::TextureManager::RemoveSprite(s_State.sprites[s_State.selectedSpriteIndex].name);
            LoadExistingSprites();
            s_State.selectedSpriteIndex = -1;
        }

    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No sprite selected");
        ImGui::Separator();
        ImGui::TextWrapped("Click to select a sprite, or click and drag to create a new sprite region.");
    }

    ImGui::EndChild();

    ImGui::Columns(1);

    // ========== POPUP: Name Entry for New Sprite ==========
    // CRITICAL FIX: Simplified popup opening logic to ensure proper registration with ImGui
    if (s_State.showNamePopup) {
        ImGui::OpenPopup("Sprite Name");
        s_State.showNamePopup = false;
        // Clear the buffer when first opening the popup
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

        // CRITICAL FIX: Only set keyboard focus once when popup first opens
        // Using a static variable to track whether we just opened this frame
        static bool justOpened = true;
        if (justOpened) {
            ImGui::SetKeyboardFocusHere();
            justOpened = false;
        }

        const bool enterPressed = ImGui::InputText("##name", s_State.spriteNameBuffer,
            sizeof(s_State.spriteNameBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::Separator();

        const bool nameValid = strlen(s_State.spriteNameBuffer) > 0;

        // Disable button visually if name is empty for better UX
        if (!nameValid) {
            ImGui::BeginDisabled();
        }

        if ((ImGui::Button("Create Sprite") || enterPressed) && nameValid) {
            const Engine::Math::Vec4 uvRect(
                static_cast<float>(selMinX) / s_State.currentTexture.Width,
                static_cast<float>(selMinY) / s_State.currentTexture.Height,
                static_cast<float>(selMaxX - selMinX) / s_State.currentTexture.Width,
                static_cast<float>(selMaxY - selMinY) / s_State.currentTexture.Height
            );

            const bool success = Engine::TextureManager::CreateSpriteFromTexture(
                s_State.currentTexturePath,
                s_State.spriteNameBuffer,
                uvRect
            );

            if (success) {
                LoadExistingSprites();
                justOpened = true; // Reset for next time popup opens
                ImGui::CloseCurrentPopup();
            }
        }

        if (!nameValid) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            justOpened = true; // Reset for next time popup opens
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::End();
}