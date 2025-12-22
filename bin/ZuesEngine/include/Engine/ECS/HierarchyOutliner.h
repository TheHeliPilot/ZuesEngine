#pragma once

#include "../ZuesAPI.h"

#include "World.h"
#include "Components.h"
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine::ECS::Hierarchy {

    // --- Private Cache Structure ---
    // This structure holds the pre-calculated hierarchy data.
    struct ZUES_API HierarchyCacheData {
        // Map: Parent EntityID -> List of Child EntityIDs
        std::unordered_map<EntityID, std::vector<EntityID>> parentToChildren;
        // List of all entities that have no parent
        std::vector<EntityID> rootEntities;
    };

    // --- New Structure for UI/Editor Traversal ---
    // Represents one line item in the flattened scene hierarchy display.
    struct ZUES_API HierarchyNode {
        EntityID id;
        int depth; // 0 for root, 1 for first child, etc.
    };

    // Static instance of the cache. Declared as 'extern' here and defined in the CPP file.
    extern ZUES_API HierarchyCacheData s_cache;

    /**
     * @brief Builds and updates the internal hierarchy cache by iterating over all TransformComponents.
     * @param world The ECS World instance.
     */
    ZUES_API void BuildCache(World* world);

    /**
     * @brief Generates a flat, ordered list representing the entire scene hierarchy structure.
     * This ensures a stable display order by sorting roots and children by EntityID.
     * @return A vector of HierarchyNode, where each node has the EntityID and its depth.
     */
    ZUES_API std::vector<HierarchyNode> GetFlattenedHierarchy();

    /**
     * @brief Traverses the HIERARCHY CACHE starting from the root entities in a depth-first manner,
     * applying the provided action for each entity.
     * @param action The function to execute for each entity. Signature: (EntityID entityID, int depth).
     */
    ZUES_API void Traverse(std::function<void(EntityID, int)> action);


    /**
     * @brief Finds all children (direct and indirect) of a specified parent entity using the CACHE.
     * @param parentID The ID of the entity to find children for.
     * @return A vector of all descendant EntityIDs.
     */
    ZUES_API std::vector<EntityID> GetAllChildren(EntityID parentID);

    /**
     * @brief Gets the immediate direct children of a specified parent entity using the CACHE.
     * @param parentID The ID of the entity to find direct children for.
     * @return A const reference to a vector of direct child EntityIDs.
     */
    ZUES_API const std::vector<EntityID>& GetDirectChildren(EntityID parentID);

} // namespace Engine::ECS::Hierarchy
