#include "../include/Engine/ECS/HierarchyOutliner.h"

#include "../include/Engine/ECS/Entity.h"
#include "../include/Engine/EngineDefines.h"

namespace Engine::ECS::Hierarchy {

    // Define the static cache storage declared as 'extern' in the header
    HierarchyCacheData s_cache;

    // --- Helper for Depth-First Traversal and Flattening ---
    // Recursively adds children to the flattened list, used by GetFlattenedHierarchy.
    static void TraverseAndFlatten(
        EntityID currentID, 
        int depth, 
        std::vector<HierarchyNode>& flattenedList) 
    {
        // 1. Add the current entity to the flat list
        flattenedList.push_back({currentID, depth});

        // 2. Recurse into children if they exist
        auto it = s_cache.parentToChildren.find(currentID);
        if (it != s_cache.parentToChildren.end()) {
            // The children are already sorted inside BuildCache, so we just iterate.
            for (EntityID childID : it->second) {
                TraverseAndFlatten(childID, depth + 1, flattenedList);
            }
        }
    }


    // --- Public Cache Builder (The core logic) ---
    void BuildCache(World* world) {
        if (!world) {
            LOG_ERROR("ERROR: Cannot build hierarchy cache, World is null.");
            return;
        }

        // 1. Clear previous state for a fresh build
        s_cache.parentToChildren.clear();
        s_cache.rootEntities.clear();
        
        // 2. Iterate over all entities that have a TransformComponent.
        // TransformComponent contains the 'parent' EntityID, which defines the hierarchy.
        world->ForEach<TransformComponent*>(
            [&](EntityID entityID, TransformComponent* transformComp) {
                if (transformComp->parent.IsValid()) {
                    // This entity has a parent, add it as a child to the parent's list.
                    s_cache.parentToChildren[transformComp->parent].push_back(entityID);
                } else {
                    // No valid parent, this is a root entity.
                    s_cache.rootEntities.push_back(entityID);
                }
            }
        );

        // 3. Post-processing: Sort all children lists within the cache.
        // This ensures the traversal order (and thus the editor UI) is stable.
        for (auto& pair : s_cache.parentToChildren) {
             std::sort(pair.second.begin(), pair.second.end(), 
                      [](const EntityID& a, const EntityID& b) {
                          // Sort by the underlying 64-bit ID
                          return a.id < b.id; 
                      });
        }
    }

    /**
     * @brief Generates a flat, ordered list representing the entire scene hierarchy structure.
     */
    std::vector<HierarchyNode> GetFlattenedHierarchy() {
        std::vector<HierarchyNode> flattenedList;
        
        // 1. Sort the main root IDs for stable hierarchy view
        std::vector<EntityID> sortedRoots = s_cache.rootEntities;
        std::sort(sortedRoots.begin(), sortedRoots.end(), 
                  [](const EntityID& a, const EntityID& b) {
                      return a.id < b.id; 
                  });

        // 2. Traverse from each sorted root
        for (EntityID rootID : sortedRoots) {
            TraverseAndFlatten(rootID, 0, flattenedList);
        }

        return flattenedList;
    }


    /**
     * @brief Traverses the HIERARCHY CACHE starting from the root entities in a depth-first manner.
     */
    void Traverse(std::function<void(EntityID, int)> action) {
        // --- Traverse using DFS starting from roots ---
        std::function<void(EntityID, int)> dfs = 
            [&](EntityID currentID, int depth) {
            
            action(currentID, depth);
            
            // Recurse into children if they exist (children are already sorted in BuildCache)
            if (s_cache.parentToChildren.count(currentID)) {
                for (EntityID childID : s_cache.parentToChildren.at(currentID)) {
                    dfs(childID, depth + 1);
                }
            }
        };
        
        // Sort the main root IDs for stable hierarchy view before traversing
        std::vector<EntityID> sortedRoots = s_cache.rootEntities;
        std::sort(sortedRoots.begin(), sortedRoots.end(), 
                  [](const EntityID& a, const EntityID& b) {
                      return a.id < b.id; 
                  });

        for (EntityID rootID : sortedRoots) {
            dfs(rootID, 0);
        }
    }


    /**
     * @brief Finds all children (direct and indirect) of a specified parent entity using the CACHE.
     */
    std::vector<EntityID> GetAllChildren(EntityID parentID) {
        std::vector<EntityID> allChildren;
        
        // Recursive helper to collect children using the cache
        std::function<void(EntityID)> collectChildren = 
            [&](EntityID currentID) {
            if (s_cache.parentToChildren.count(currentID)) {
                // Children are already sorted, so traversal is stable
                for (EntityID childID : s_cache.parentToChildren.at(currentID)) {
                    allChildren.push_back(childID);
                    collectChildren(childID); // Recurse
                }
            }
        };

        collectChildren(parentID);
        return allChildren;
    }

    /**
     * @brief Gets the immediate direct children of a specified parent entity using the CACHE.
     */
    const std::vector<EntityID>& GetDirectChildren(EntityID parentID) {
        // Return an empty vector reference if the parent ID is not found in the map (no children)
        // Using a static const empty vector avoids heap allocation every time an entity has no children.
        static const std::vector<EntityID> emptyChildren;
        
        if (s_cache.parentToChildren.count(parentID)) {
            return s_cache.parentToChildren.at(parentID);
        }
        return emptyChildren;
    }

} // namespace Engine::ECS::Hierarchy
