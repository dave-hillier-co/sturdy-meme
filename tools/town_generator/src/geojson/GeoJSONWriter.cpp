#include "town_generator/geojson/GeoJSONWriter.h"

#include "town_generator/building/City.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/WardGroup.h"
#include "town_generator/building/Block.h"
#include "town_generator/wards/Ward.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <SDL3/SDL.h>

namespace town_generator {
namespace geojson {

namespace {

// Hand-rolled JSON emission with fixed precision so output is deterministic
// and diff-friendly (nlohmann would print shortest-roundtrip doubles).
class Emitter {
public:
    explicit Emitter(const Placement& placement, double cityCenterX, double cityCenterY)
        : placement_(placement), cx_(cityCenterX), cy_(cityCenterY) {
        out_ << std::fixed << std::setprecision(2);
    }

    void coord(double localX, double localY) {
        double x = placement_.centerX + (localX - cx_) * placement_.scale;
        double y = placement_.centerY + (localY - cy_) * placement_.scale;
        out_ << "[" << x << "," << y << "]";
    }

    template <typename PointRange>
    void ring(const PointRange& points) {
        out_ << "[";
        bool first = true;
        for (const auto& p : points) {
            if (!first) out_ << ",";
            coord(pointX(p), pointY(p));
            first = false;
        }
        // Close the ring (GeoJSON polygons repeat the first vertex)
        if (!first) {
            out_ << ",";
            coord(pointX(*points.begin()), pointY(*points.begin()));
        }
        out_ << "]";
    }

    template <typename PointRange>
    void lineCoords(const PointRange& points) {
        out_ << "[";
        bool first = true;
        for (const auto& p : points) {
            if (!first) out_ << ",";
            coord(pointX(p), pointY(p));
            first = false;
        }
        out_ << "]";
    }

    void beginFeature(const char* geometryType) {
        if (featureCount_ > 0) out_ << ",";
        out_ << "\n    {\"type\":\"Feature\",\"geometry\":{\"type\":\"" << geometryType
             << "\",\"coordinates\":";
        featureCount_++;
    }

    void endFeature(const std::string& propertiesJson) {
        out_ << "},\"properties\":" << propertiesJson << "}";
    }

    std::ostringstream& stream() { return out_; }
    size_t featureCount() const { return featureCount_; }

private:
    static double pointX(const geom::Point& p) { return p.x; }
    static double pointY(const geom::Point& p) { return p.y; }
    static double pointX(const geom::PointPtr& p) { return p->x; }
    static double pointY(const geom::PointPtr& p) { return p->y; }

    std::ostringstream out_;
    const Placement& placement_;
    double cx_, cy_;
    size_t featureCount_ = 0;
};

std::string buildingProps(const std::string& ward, bool special) {
    std::ostringstream ss;
    ss << "{\"kind\":\"building\",\"ward\":\"" << ward << "\",\"special\":"
       << (special ? "true" : "false") << "}";
    return ss.str();
}

std::string lineProps(const char* kind) {
    std::ostringstream ss;
    ss << "{\"kind\":\"" << kind << "\"}";
    return ss.str();
}

} // namespace

geom::Rectangle GeoJSONWriter::buildingBounds(const building::City& model) {
    geom::Rectangle rect;
    bool first = true;

    auto expand = [&](const geom::Polygon& poly) {
        for (size_t i = 0; i < poly.length(); ++i) {
            if (first) {
                rect = geom::Rectangle(poly[i].x, poly[i].y);
                first = false;
            } else {
                rect.left = std::min(rect.left, poly[i].x);
                rect.right = std::max(rect.right, poly[i].x);
                rect.top = std::min(rect.top, poly[i].y);
                rect.bottom = std::max(rect.bottom, poly[i].y);
            }
        }
    };

    for (const auto& group : model.wardGroups_) {
        for (const auto& block : group->blocks) {
            for (const auto& b : block->buildings) expand(b);
        }
    }
    for (const auto& ward : model.wards_) {
        if (ward->getName() == "Alleys" && !ward->isSpecialWard()) continue;
        for (const auto& b : ward->geometry) expand(b);
    }

    return rect;
}

double GeoJSONWriter::medianBuildingSize(const building::City& model) {
    std::vector<double> sizes;

    // Short side of the longest-edge-oriented bounding box: the wall-to-wall
    // width a player actually perceives (sqrt(area) under-sizes the many
    // elongated burgage-plot buildings)
    auto addSize = [&](const geom::Polygon& poly) {
        if (poly.length() < 3) return;
        double bx = 1.0, by = 0.0, bestLen2 = 0.0;
        for (size_t i = 0; i < poly.length(); ++i) {
            const auto& a = poly[i];
            const auto& b = poly[(i + 1) % poly.length()];
            double ex = b.x - a.x, ey = b.y - a.y;
            double len2 = ex * ex + ey * ey;
            if (len2 > bestLen2) { bestLen2 = len2; bx = ex; by = ey; }
        }
        if (bestLen2 <= 0.0) return;
        double len = std::sqrt(bestLen2);
        bx /= len; by /= len;
        double minU = 1e18, maxU = -1e18, minV = 1e18, maxV = -1e18;
        for (size_t i = 0; i < poly.length(); ++i) {
            double u = poly[i].x * bx + poly[i].y * by;
            double v = -poly[i].x * by + poly[i].y * bx;
            minU = std::min(minU, u); maxU = std::max(maxU, u);
            minV = std::min(minV, v); maxV = std::max(maxV, v);
        }
        double width = std::min(maxU - minU, maxV - minV);
        if (width > 0.0) sizes.push_back(width);
    };

    for (const auto& group : model.wardGroups_) {
        for (const auto& block : group->blocks) {
            for (const auto& b : block->buildings) addSize(b);
        }
    }
    for (const auto& ward : model.wards_) {
        if (ward->getName() == "Alleys" && !ward->isSpecialWard()) continue;
        for (const auto& b : ward->geometry) addSize(b);
    }

    if (sizes.empty()) return 0.0;
    std::sort(sizes.begin(), sizes.end());
    return sizes[sizes.size() / 2];
}

bool GeoJSONWriter::write(const building::City& model,
                          const Placement& placement,
                          const std::string& path) {
    geom::Rectangle bounds = buildingBounds(model);
    if (bounds.width() <= 0.0 && bounds.height() <= 0.0) {
        bounds = model.borderPatch.shape.getBounds();
    }
    double cityCenterX = (bounds.left + bounds.right) * 0.5;
    double cityCenterY = (bounds.top + bounds.bottom) * 0.5;

    Emitter em(placement, cityCenterX, cityCenterY);

    // Buildings from ward groups (Alleys wards)
    for (const auto& group : model.wardGroups_) {
        for (const auto& block : group->blocks) {
            for (const auto& b : block->buildings) {
                if (b.length() < 3) continue;
                em.beginFeature("Polygon");
                em.stream() << "[";
                em.ring(b);
                em.stream() << "]";
                em.endFeature(buildingProps("Alleys", false));
            }
        }
    }

    // Buildings from other wards (Farm, Harbour, Market, ...), churches, castles
    for (const auto& ward : model.wards_) {
        std::string name = ward->getName();
        if (name == "Alleys" && !ward->isSpecialWard()) continue;  // From groups above

        for (const auto& b : ward->geometry) {
            if (b.length() < 3) continue;
            bool isChurch = ward->hasChurch() && &b == &ward->getChurch();
            em.beginFeature("Polygon");
            em.stream() << "[";
            em.ring(b);
            em.stream() << "]";
            em.endFeature(buildingProps(name, ward->isSpecialWard() || isChurch));
        }
    }

    // Streets
    auto emitStreets = [&](const std::vector<building::City::Street>& lines, const char* kind) {
        for (const auto& line : lines) {
            if (line.size() < 2) continue;
            em.beginFeature("LineString");
            em.lineCoords(line);
            em.endFeature(lineProps(kind));
        }
    };
    emitStreets(model.arteries, "artery");
    emitStreets(model.roads, "road");
    emitStreets(model.streets, "street");

    for (const auto& ward : model.wards_) {
        for (const auto& alley : ward->alleys) {
            if (alley.size() < 2) continue;
            em.beginFeature("LineString");
            em.lineCoords(alley);
            em.endFeature(lineProps("alley"));
        }
    }

    // Walls (inner wall and citadel if present)
    auto emitWall = [&](const building::CurtainWall* wall) {
        if (!wall || wall->shape.length() < 3) return;
        em.beginFeature("LineString");
        // A curtain wall is a closed circuit: repeat the first vertex so the
        // runtime extrudes the segment back to the start.
        em.ring(wall->shape);
        em.endFeature(lineProps("wall"));
    };
    emitWall(model.wall);
    emitWall(model.citadel);

    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GeoJSONWriter: Failed to open %s", path.c_str());
        return false;
    }

    file << std::fixed << std::setprecision(2);
    file << "{\n\"type\":\"FeatureCollection\",\n\"properties\":{"
         << "\"settlement_id\":" << placement.settlementId
         << ",\"settlement_type\":\"" << placement.settlementType << "\""
         << ",\"seed\":" << placement.seed
         << ",\"scale\":" << placement.scale
         << ",\"extent_radius\":" << placement.extentRadius
         << ",\"center\":[" << placement.centerX << "," << placement.centerY << "]"
         << "},\n\"features\":[";
    file << em.stream().str();
    file << "\n]}\n";

    SDL_Log("GeoJSONWriter: Wrote %zu features to %s", em.featureCount(), path.c_str());
    return true;
}

} // namespace geojson
} // namespace town_generator
