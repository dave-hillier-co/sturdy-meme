#pragma once

#include "elevation_grid.h"
#include <vector>
#include <cstdint>

// A lake: a closed depression in the DEM filled to its spill elevation.
struct Lake {
    // Outer boundary ring in processing-grid vertex coordinates (cell-edge
    // contour, closed: first point == last point).
    std::vector<std::pair<float, float>> boundary;
    uint16_t fill_level = 0;      // Water surface elevation (raw uint16)
    uint16_t min_elevation = 0;   // Deepest cell elevation (raw uint16)
    uint32_t area_cells = 0;      // Lake area in processing cells
    float centroid_x = 0.0f;      // Centroid in processing-grid coords
    float centroid_y = 0.0f;
};

// Extract lakes from the DEM via priority-flood depression filling.
// Cells whose fill level exceeds their elevation form lakes; each
// 4-connected component becomes one Lake. Lakes whose water surface would
// be at or below sea_level are discarded (that is sea, not lake), as are
// components smaller than min_area_cells.
std::vector<Lake> extract_lakes(
    const ElevationGrid& elevation,
    uint16_t sea_level,
    uint32_t min_area_cells
);
