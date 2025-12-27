#pragma once

#include "EngineDefines.h"
#include "ZuesMath.h"
#include <string>

namespace Engine {
namespace Debug {

    // Screen-space debug drawing functions
    // All sizes are in screen pixels, automatically converted to world units based on camera
    // These are available in Engine so both Editor and Project code can use them

    // Convert screen pixels to world units based on current camera
    ZUES_API float ScreenToWorld(float screenPixels);

    // Draw a line between two world positions with screen-pixel thickness
    ZUES_API void DrawLine(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float screenThickness = 1.5f);

    // Draw an unfilled circle at world position with screen-pixel thickness
    ZUES_API void DrawCircle(const Math::Vec2& center, float worldRadius, const Math::Vec4& color, int segments = 32, float screenThickness = 1.5f);

    // Draw an unfilled rectangle at world position with screen-pixel thickness
    ZUES_API void DrawRect(const Math::Vec2& center, const Math::Vec2& worldSize, const Math::Vec4& color, float rotationRad = 0.0f, float screenThickness = 1.5f);

    // Draw a filled circle at world position with screen-pixel radius
    ZUES_API void DrawFilledCircle(const Math::Vec2& center, float screenRadius, const Math::Vec4& color);

    // Draw a filled rectangle at world position with screen-pixel size
    ZUES_API void DrawFilledRect(const Math::Vec2& center, float screenWidth, float screenHeight, const Math::Vec4& color, float rotationRad = 0.0f);

    // Draw an arrow from start to end with screen-pixel thickness and head size
    ZUES_API void DrawArrow(const Math::Vec2& start, const Math::Vec2& end, const Math::Vec4& color, float screenThickness = 2.0f, float screenHeadSize = 8.0f);

    // Draw a cross/X marker at position with screen-pixel size
    ZUES_API void DrawCross(const Math::Vec2& center, const Math::Vec4& color, float screenSize = 10.0f, float screenThickness = 1.5f);

    // Draw a plus + marker at position with screen-pixel size
    ZUES_API void DrawPlus(const Math::Vec2& center, const Math::Vec4& color, float screenSize = 10.0f, float screenThickness = 1.5f);

    // Draw a point/dot at position with screen-pixel radius
    ZUES_API void DrawPoint(const Math::Vec2& center, const Math::Vec4& color, float screenRadius = 4.0f);

    // Draw text at world position (wrapper around Renderer::DrawText with screen-space scaling)
    ZUES_API void DrawText(const std::string& text, const Math::Vec2& position, const Math::Vec4& color, float screenFontSize = 14.0f, uint32_t fontID = 0);

    // Draw a wireframe box (3D-style with depth lines) - useful for showing bounds
    ZUES_API void DrawWireBox(const Math::Vec2& center, const Math::Vec2& worldSize, const Math::Vec4& color, float rotationRad = 0.0f, float screenThickness = 1.5f);

    // Draw a grid pattern
    ZUES_API void DrawGrid(const Math::Vec2& center, float worldCellSize, int cellCountX, int cellCountY, const Math::Vec4& color, float screenThickness = 1.0f);

} // namespace Debug
} // namespace Engine
