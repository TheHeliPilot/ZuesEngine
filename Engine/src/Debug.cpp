#include "../include/Engine/Debug.h"
#include "../include/Engine/Renderer.h"
#include <cmath>

namespace Engine {
namespace Debug {

    float ScreenToWorld(float screenPixels) {
        return Renderer::ScreenToWorldSize(screenPixels);
    }

    void DrawLine(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float screenThickness) {
        float thickness = ScreenToWorld(screenThickness);
        Renderer::DrawLine(start, end, color, thickness);
    }

    void DrawCircle(const Math::Vec2& center, float worldRadius, const Math::Vec4& color, int segments, float screenThickness) {
        float thickness = ScreenToWorld(screenThickness);
        Renderer::DrawCircle(center, worldRadius, color, segments, true, thickness);
    }

    void DrawRect(const Math::Vec2& center, const Math::Vec2& worldSize, const Math::Vec4& color, float rotationRad, float screenThickness) {
        float thickness = ScreenToWorld(screenThickness);
        float cosR = std::cos(rotationRad);
        float sinR = std::sin(rotationRad);
        float halfW = worldSize.x / 2.0f;
        float halfH = worldSize.y / 2.0f;

        auto rotatePoint = [&](float lx, float ly) -> Math::Vec2 {
            return {
                center.x + lx * cosR - ly * sinR,
                center.y + lx * sinR + ly * cosR
            };
        };

        Math::Vec2 tl = rotatePoint(-halfW, halfH);
        Math::Vec2 tr = rotatePoint(halfW, halfH);
        Math::Vec2 br = rotatePoint(halfW, -halfH);
        Math::Vec2 bl = rotatePoint(-halfW, -halfH);

        Renderer::DrawLine(tl, tr, color, thickness);
        Renderer::DrawLine(tr, br, color, thickness);
        Renderer::DrawLine(br, bl, color, thickness);
        Renderer::DrawLine(bl, tl, color, thickness);
    }

    void DrawFilledCircle(const Math::Vec2& center, float screenRadius, const Math::Vec4& color) {
        float worldRadius = ScreenToWorld(screenRadius);
        Renderer::DrawCircle(center, worldRadius, color, 16, false, 0);
    }

    void DrawFilledRect(const Math::Vec2& center, float screenWidth, float screenHeight, const Math::Vec4& color, float rotationRad) {
        float worldW = ScreenToWorld(screenWidth);
        float worldH = ScreenToWorld(screenHeight);
        Renderer::SubmitQuad(center, rotationRad, {worldW, worldH}, color, 0, 0.99f);
    }

    void DrawArrow(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float screenThickness, float screenHeadSize) {
        float thickness = ScreenToWorld(screenThickness);
        float headSize = ScreenToWorld(screenHeadSize);

        // Draw main line
        Renderer::DrawLine(start, end, color, thickness);

        // Draw arrowhead
        Math::Vec2 dir = {end.x - start.x, end.y - start.y};
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 0.001f) {
            dir.x /= len;
            dir.y /= len;
        } else {
            return; // Arrow too short
        }

        Math::Vec2 perp = {-dir.y, dir.x};
        Math::Vec2 left = {end.x - dir.x * headSize + perp.x * headSize * 0.5f,
                          end.y - dir.y * headSize + perp.y * headSize * 0.5f};
        Math::Vec2 right = {end.x - dir.x * headSize - perp.x * headSize * 0.5f,
                           end.y - dir.y * headSize - perp.y * headSize * 0.5f};

        Renderer::DrawLine(end, left, color, thickness);
        Renderer::DrawLine(end, right, color, thickness);
        Renderer::DrawLine(left, right, color, thickness);
    }

    void DrawCross(const Math::Vec2& center, const Math::Vec4& color, float screenSize, float screenThickness) {
        float size = ScreenToWorld(screenSize) * 0.5f;
        float thickness = ScreenToWorld(screenThickness);

        // Draw X shape
        Renderer::DrawLine({center.x - size, center.y - size}, {center.x + size, center.y + size}, color, thickness);
        Renderer::DrawLine({center.x - size, center.y + size}, {center.x + size, center.y - size}, color, thickness);
    }

    void DrawPlus(const Math::Vec2& center, const Math::Vec4& color, float screenSize, float screenThickness) {
        float size = ScreenToWorld(screenSize) * 0.5f;
        float thickness = ScreenToWorld(screenThickness);

        // Draw + shape
        Renderer::DrawLine({center.x - size, center.y}, {center.x + size, center.y}, color, thickness);
        Renderer::DrawLine({center.x, center.y - size}, {center.x, center.y + size}, color, thickness);
    }

    void DrawPoint(const Math::Vec2& center, const Math::Vec4& color, float screenRadius) {
        float worldRadius = ScreenToWorld(screenRadius);
        Renderer::DrawCircle(center, worldRadius, color, 12, false, 0);
    }

    void DrawText(const std::string& text, const Math::Vec2& position, const Math::Vec4& color, float screenFontSize, uint32_t fontID) {
        // Scale text based on screen size
        float worldScale = ScreenToWorld(1.0f); // 1 screen pixel = worldScale world units
        float scale = screenFontSize / 14.0f; // Assuming 14px is base font size

        Renderer::DrawText(fontID, text, position, color, scale, worldScale, 0);
    }

    void DrawWireBox(const Math::Vec2& center, const Math::Vec2& worldSize, const Math::Vec4& color, float rotationRad, float screenThickness) {
        // For 2D, this is just a rectangle outline
        DrawRect(center, worldSize, color, rotationRad, screenThickness);
    }

    void DrawGrid(const Math::Vec2& center, float worldCellSize, int cellCountX, int cellCountY, const Math::Vec4& color, float screenThickness) {
        float thickness = ScreenToWorld(screenThickness);

        float totalWidth = worldCellSize * cellCountX;
        float totalHeight = worldCellSize * cellCountY;
        float startX = center.x - totalWidth * 0.5f;
        float startY = center.y - totalHeight * 0.5f;

        // Draw vertical lines
        for (int i = 0; i <= cellCountX; i++) {
            float x = startX + i * worldCellSize;
            Renderer::DrawLine({x, startY}, {x, startY + totalHeight}, color, thickness);
        }

        // Draw horizontal lines
        for (int i = 0; i <= cellCountY; i++) {
            float y = startY + i * worldCellSize;
            Renderer::DrawLine({startX, y}, {startX + totalWidth, y}, color, thickness);
        }
    }

} // namespace Debug
} // namespace Engine
