#pragma once

#include "town_generator/geom/Polygon.h"

#include <string>
#include <cstdint>

namespace town_generator {
namespace building { class City; }

namespace geojson {

// Placement of the town-local layout into world content space (0..terrain_size).
// Output coordinates = center + (local - cityCenter) * scale, so consumers
// (engine, world_preview) read the same content-space convention as
// roads.geojson and settlements.json.
struct Placement {
    double centerX = 0.0;
    double centerY = 0.0;              // Content-space Z
    double scale = 1.0;                // Local units -> meters
    double extentRadius = 0.0;         // Built-up radius in meters after scaling
    uint32_t settlementId = 0;
    std::string settlementType;
    int seed = 0;
};

// Writes the city layout as a GeoJSON FeatureCollection:
// - building footprints: Polygon features {kind:"building", ward, special}
// - streets: LineString features {kind:"artery"|"road"|"street"|"alley"}
// - walls: LineString features {kind:"wall"}
// Output is deterministic for a fixed model (iteration follows model order,
// coordinates rounded to centimeters).
class GeoJSONWriter {
public:
    static bool write(const building::City& model,
                      const Placement& placement,
                      const std::string& path);

    // Bounding box over all building footprints (the built-up area, which is
    // much smaller than the city border patch with its wilderness ring). Use
    // this to derive the placement scale so buildings fill the settlement.
    static geom::Rectangle buildingBounds(const building::City& model);

    // Median building short side (wall-to-wall width of the longest-edge
    // oriented bounding box), in model-local units. Used to scale towns so
    // buildings come out at realistic sizes rather than compressing large
    // layouts into the nominal settlement radius.
    static double medianBuildingSize(const building::City& model);
};

} // namespace geojson
} // namespace town_generator
