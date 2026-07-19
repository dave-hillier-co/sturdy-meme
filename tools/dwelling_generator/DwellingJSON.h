#pragma once

#include "DwellingHouse.h"
#include <string>

namespace dwelling {

// Write the full house plan as JSON (standard interchange format for the
// runtime interiors pipeline): per-floor rooms with grid-cell areas and
// contour edges, doors (with type and exterior flag), windows, and stairs.
// Grid coordinates are cell indices; the consumer scales cells to meters.
bool writeDwellingJson(const std::string& path, const House& house);

} // namespace dwelling
