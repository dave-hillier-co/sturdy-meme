#pragma once

#include "Ward.h"
#include "../building/CurtainWall.h"
#include "../building/Model.h"
#include <vector>
#include <memory>

namespace towngenerator {
namespace wards {

/**
 * Castle - Citadel/fortress ward with defensive walls
 * Ported from Haxe Castle class
 *
 * Creates a large central building surrounded by curtain walls
 */
class Castle : public Ward {
public:
    std::unique_ptr<CurtainWall> wall;

    /**
     * Constructor
     * @param model The city model
     * @param patch The patch this castle occupies
     *
     * Creates curtain walls around the castle patch, with gates at vertices
     * that border patches outside the city
     */
    Castle(Model* model, Patch* patch)
        : Ward(model, patch)
        , wall(nullptr)
    {
        // Filter vertices to find those bordering patches outside the city
        std::vector<Point> reserved;

        for (const auto& vertex : patch->shape.vertices) {
            // Get all patches that share this vertex
            auto adjacentPatches = model->patchByVertex(vertex);

            // Check if any adjacent patch is outside the city
            bool bordersOutside = false;
            for (auto* adjacentPatch : adjacentPatches) {
                if (!adjacentPatch->withinCity) {
                    bordersOutside = true;
                    break;
                }
            }

            // If this vertex borders an outside patch, add to reserved (for gates)
            if (bordersOutside) {
                reserved.push_back(vertex);
            }
        }

        // Create curtain wall around this patch
        std::vector<Patch*> patches = {patch};
        wall = std::make_unique<CurtainWall>(
            true,           // realistic mode
            *model,         // city model
            patches,        // patches to enclose
            reserved        // vertices that can be gates
        );
    }

    /**
     * Create castle geometry
     * Large orthogonal building in the center
     */
    void createGeometry() override {
        // Shrink patch by 2 * MAIN_STREET to create building block
        Polygon block = patch->shape.shrinkEq(Ward::MAIN_STREET * 2.0f);

        // Create large orthogonal building
        // Height proportional to area, 60% fill ratio
        float height = std::sqrt(block.square()) * 4.0f;
        geometry = Ward::createOrthoBuilding(block, height, 0.6f);
    }

    /**
     * Get ward label
     */
    const char* getLabel() const override {
        return "Castle";
    }
};

} // namespace wards
} // namespace towngenerator
