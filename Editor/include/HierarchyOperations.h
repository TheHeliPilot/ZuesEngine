//
// Created by bucka on 9/30/2025.
//

#ifndef EDITOR_HIERARCHYOPERATIONS_H
#define EDITOR_HIERARCHYOPERATIONS_H
#include "ECS/World.h"

class HierarchyOperations {
public:
    enum class DraggingOperation {
        None,
        MoveX,
        MoveY,
        MoveXY,
        Rotate,
        ScaleX,
        ScaleY,
        ScaleXY,
    };

    static DraggingOperation draggingStatus;
    static Engine::Math::Vec2 lastMousePos;

    static void DoHierarchyOperations();
};

#endif //EDITOR_HIERARCHYOPERATIONS_H