#include "SplineRasterizer.h"
#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VirtualTexture {

// RasterizedTile implementations
bool RasterizedTile::hasRoads() const {
    for (float m : roadMask) {
        if (m > 0.001f) return true;
    }
    return false;
}

bool RasterizedTile::hasRiverbeds() const {
    for (float m : riverbedMask) {
        if (m > 0.001f) return true;
    }
    return false;
}

float RasterizedTile::sampleRoadMask(uint32_t x, uint32_t y) const {
    if (roadMask.empty()) return 0.0f;
    size_t idx = pixelIndex(x, y);
    return idx < roadMask.size() ? roadMask[idx] : 0.0f;
}

glm::vec2 RasterizedTile::sampleRoadUV(uint32_t x, uint32_t y) const {
    if (roadUVs.empty()) return glm::vec2(0.0f);
    size_t idx = pixelIndex(x, y);
    return idx < roadUVs.size() ? roadUVs[idx] : glm::vec2(0.0f);
}

RoadGen::RoadType RasterizedTile::sampleRoadType(uint32_t x, uint32_t y) const {
    if (roadTypes.empty()) return RoadGen::RoadType::Lane;
    size_t idx = pixelIndex(x, y);
    return idx < roadTypes.size() ? static_cast<RoadGen::RoadType>(roadTypes[idx]) : RoadGen::RoadType::Lane;
}

float RasterizedTile::sampleRiverbedMask(uint32_t x, uint32_t y) const {
    if (riverbedMask.empty()) return 0.0f;
    size_t idx = pixelIndex(x, y);
    return idx < riverbedMask.size() ? riverbedMask[idx] : 0.0f;
}

glm::vec2 RasterizedTile::sampleRiverbedUV(uint32_t x, uint32_t y) const {
    if (riverbedUVs.empty()) return glm::vec2(0.0f);
    size_t idx = pixelIndex(x, y);
    return idx < riverbedUVs.size() ? riverbedUVs[idx] : glm::vec2(0.0f);
}

// SplineRasterizer implementations
SplineRasterizer::SplineRasterizer() = default;

void SplineRasterizer::init(const SplineRasterizerConfig& cfg) {
    config = cfg;
}

void SplineRasterizer::setRoads(const std::vector<RoadGen::RoadSpline>& roadSplines) {
    roads = roadSplines;
    buildRoadData();
    SDL_Log("SplineRasterizer: loaded %zu roads", roads.size());
}

void SplineRasterizer::setRivers(const std::vector<RiverSpline>& riverSplines) {
    rivers = riverSplines;
    buildRiverData();
    SDL_Log("SplineRasterizer: loaded %zu rivers", rivers.size());
}

void SplineRasterizer::buildRoadData() {
    roadData.clear();
    roadData.reserve(roads.size());

    for (const auto& road : roads) {
        if (road.controlPoints.size() < 2) continue;

        RoadData data;
        data.type = road.type;
        data.totalLength = 0.0f;

        // Initialize bounds
        data.bounds.min = glm::vec2(std::numeric_limits<float>::max());
        data.bounds.max = glm::vec2(std::numeric_limits<float>::lowest());

        // Build segments
        float t = 0.0f;
        for (size_t i = 1; i < road.controlPoints.size(); i++) {
            const auto& cp0 = road.controlPoints[i - 1];
            const auto& cp1 = road.controlPoints[i];

            SplineSegment seg;
            seg.p0 = cp0.position;
            seg.p1 = cp1.position;
            seg.w0 = road.getWidthAt(i - 1);
            seg.w1 = road.getWidthAt(i);
            seg.t0 = t;

            float segLength = glm::length(seg.p1 - seg.p0);
            t += segLength;
            seg.t1 = t;

            // Compute segment bounds with width padding
            float maxWidth = std::max(seg.w0, seg.w1);
            seg.bounds.min = glm::min(seg.p0, seg.p1) - glm::vec2(maxWidth);
            seg.bounds.max = glm::max(seg.p0, seg.p1) + glm::vec2(maxWidth);

            // Update road bounds
            data.bounds.min = glm::min(data.bounds.min, seg.bounds.min);
            data.bounds.max = glm::max(data.bounds.max, seg.bounds.max);

            data.segments.push_back(seg);
        }

        data.totalLength = t;
        roadData.push_back(std::move(data));
    }
}

void SplineRasterizer::buildRiverData() {
    riverData.clear();
    riverData.reserve(rivers.size());

    for (const auto& river : rivers) {
        if (river.controlPoints.size() < 2) continue;

        // Skip very thin rivers
        float maxWidth = 0.0f;
        for (float w : river.widths) {
            maxWidth = std::max(maxWidth, w);
        }
        if (maxWidth < config.minRiverWidth) continue;

        RiverData data;
        data.totalLength = 0.0f;

        // Initialize bounds
        data.bounds.min = glm::vec2(std::numeric_limits<float>::max());
        data.bounds.max = glm::vec2(std::numeric_limits<float>::lowest());

        // Build segments
        float t = 0.0f;
        for (size_t i = 1; i < river.controlPoints.size(); i++) {
            const auto& cp0 = river.controlPoints[i - 1];
            const auto& cp1 = river.controlPoints[i];

            SplineSegment seg;
            seg.p0 = glm::vec2(cp0.x, cp0.z);  // controlPoints are vec3 (x, height, z)
            seg.p1 = glm::vec2(cp1.x, cp1.z);

            // Get widths, apply riverbed multiplier
            seg.w0 = (i - 1 < river.widths.size() ? river.widths[i - 1] : 5.0f) * config.riverbedWidthMultiplier;
            seg.w1 = (i < river.widths.size() ? river.widths[i] : 5.0f) * config.riverbedWidthMultiplier;

            seg.t0 = t;
            float segLength = glm::length(seg.p1 - seg.p0);
            t += segLength;
            seg.t1 = t;

            // Compute segment bounds with width padding
            float maxWidth = std::max(seg.w0, seg.w1);
            seg.bounds.min = glm::min(seg.p0, seg.p1) - glm::vec2(maxWidth);
            seg.bounds.max = glm::max(seg.p0, seg.p1) + glm::vec2(maxWidth);

            // Update river bounds
            data.bounds.min = glm::min(data.bounds.min, seg.bounds.min);
            data.bounds.max = glm::max(data.bounds.max, seg.bounds.max);

            data.segments.push_back(seg);
        }

        data.totalLength = t;
        riverData.push_back(std::move(data));
    }
}

bool SplineRasterizer::tileHasRoads(uint32_t tileX, uint32_t tileY) const {
    TileBounds tileBounds = config.getTileBounds(tileX, tileY);

    for (const auto& road : roadData) {
        // Check if road bounds intersect tile bounds
        if (road.bounds.max.x < tileBounds.min.x || road.bounds.min.x > tileBounds.max.x ||
            road.bounds.max.y < tileBounds.min.y || road.bounds.min.y > tileBounds.max.y) {
            continue;
        }
        return true;
    }
    return false;
}

bool SplineRasterizer::tileHasRivers(uint32_t tileX, uint32_t tileY) const {
    TileBounds tileBounds = config.getTileBounds(tileX, tileY);

    for (const auto& river : riverData) {
        // Check if river bounds intersect tile bounds
        if (river.bounds.max.x < tileBounds.min.x || river.bounds.min.x > tileBounds.max.x ||
            river.bounds.max.y < tileBounds.min.y || river.bounds.min.y > tileBounds.max.y) {
            continue;
        }
        return true;
    }
    return false;
}

SplineQueryResult SplineRasterizer::querySegment(const SplineSegment& seg, glm::vec2 point) const {
    SplineQueryResult result;

    glm::vec2 segDir = seg.p1 - seg.p0;
    float segLength = glm::length(segDir);

    if (segLength < 0.0001f) {
        // Degenerate segment
        result.closestPoint = seg.p0;
        result.distance = glm::length(point - seg.p0);
        result.t = seg.t0;
        result.width = seg.w0;
        result.segmentIndex = -1;
        return result;
    }

    segDir /= segLength;

    // Project point onto segment line
    glm::vec2 toPoint = point - seg.p0;
    float proj = glm::dot(toPoint, segDir);

    // Clamp to segment
    proj = glm::clamp(proj, 0.0f, segLength);

    // Compute closest point and distance
    result.closestPoint = seg.p0 + segDir * proj;
    result.distance = glm::length(point - result.closestPoint);

    // Interpolate t and width along segment
    float localT = proj / segLength;
    result.t = glm::mix(seg.t0, seg.t1, localT);
    result.width = glm::mix(seg.w0, seg.w1, localT);

    return result;
}

SplineQueryResult SplineRasterizer::queryRoadSpline(const RoadGen::RoadSpline& road, glm::vec2 point) const {
    SplineQueryResult best;
    best.distance = std::numeric_limits<float>::max();
    best.segmentIndex = -1;

    // Find the road in our precomputed data
    for (size_t roadIdx = 0; roadIdx < roadData.size(); roadIdx++) {
        const auto& data = roadData[roadIdx];

        // Quick bounds check
        if (point.x < data.bounds.min.x - data.segments[0].w0 ||
            point.x > data.bounds.max.x + data.segments.back().w1 ||
            point.y < data.bounds.min.y - data.segments[0].w0 ||
            point.y > data.bounds.max.y + data.segments.back().w1) {
            continue;
        }

        for (size_t i = 0; i < data.segments.size(); i++) {
            const auto& seg = data.segments[i];

            // Quick bounds check for segment
            float maxW = std::max(seg.w0, seg.w1);
            if (point.x < seg.bounds.min.x - maxW || point.x > seg.bounds.max.x + maxW ||
                point.y < seg.bounds.min.y - maxW || point.y > seg.bounds.max.y + maxW) {
                continue;
            }

            SplineQueryResult result = querySegment(seg, point);
            result.segmentIndex = static_cast<int>(i);

            if (result.distance < best.distance) {
                best = result;
            }
        }
    }

    return best;
}

SplineQueryResult SplineRasterizer::queryRiverSpline(const RiverSpline& river, glm::vec2 point) const {
    SplineQueryResult best;
    best.distance = std::numeric_limits<float>::max();
    best.segmentIndex = -1;

    for (const auto& data : riverData) {
        // Quick bounds check
        if (point.x < data.bounds.min.x || point.x > data.bounds.max.x ||
            point.y < data.bounds.min.y || point.y > data.bounds.max.y) {
            continue;
        }

        for (size_t i = 0; i < data.segments.size(); i++) {
            const auto& seg = data.segments[i];

            SplineQueryResult result = querySegment(seg, point);
            result.segmentIndex = static_cast<int>(i);

            if (result.distance < best.distance) {
                best = result;
            }
        }
    }

    return best;
}

float SplineRasterizer::smoothstep(float edge0, float edge1, float x) {
    float t = glm::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void SplineRasterizer::rasterizeRoadToTile(const RoadData& road, RasterizedTile& tile) const {
    float pixelSize = tile.bounds.width() / tile.resolution;

    for (uint32_t py = 0; py < tile.resolution; py++) {
        for (uint32_t px = 0; px < tile.resolution; px++) {
            // Convert pixel to world position
            glm::vec2 worldPos(
                tile.bounds.min.x + (px + 0.5f) * pixelSize,
                tile.bounds.min.y + (py + 0.5f) * pixelSize
            );

            // Find closest point on this road
            SplineQueryResult best;
            best.distance = std::numeric_limits<float>::max();

            for (const auto& seg : road.segments) {
                // Quick bounds check
                float maxW = std::max(seg.w0, seg.w1);
                if (worldPos.x < seg.bounds.min.x - maxW || worldPos.x > seg.bounds.max.x + maxW ||
                    worldPos.y < seg.bounds.min.y - maxW || worldPos.y > seg.bounds.max.y + maxW) {
                    continue;
                }

                SplineQueryResult result = querySegment(seg, worldPos);
                if (result.distance < best.distance) {
                    best = result;
                }
            }

            if (best.distance < std::numeric_limits<float>::max()) {
                // Compute signed distance (negative = inside road)
                float halfWidth = best.width * 0.5f;
                float sdf = best.distance - halfWidth;

                // Compute alpha with anti-aliased edge
                float alpha = 1.0f - smoothstep(-config.edgeSmoothness, config.edgeSmoothness, sdf);

                if (alpha > 0.001f) {
                    size_t idx = tile.pixelIndex(px, py);

                    // Blend with existing values (roads can overlap)
                    float existingAlpha = tile.roadMask[idx];
                    if (alpha > existingAlpha) {
                        tile.roadMask[idx] = alpha;
                        tile.roadTypes[idx] = static_cast<uint8_t>(road.type);

                        // Compute UV coordinates
                        float u = (best.distance / best.width) + 0.5f;  // 0-1 across width
                        float v = best.t * config.roadUVScale;           // Along length
                        tile.roadUVs[idx] = glm::vec2(u, v);
                    }
                }
            }
        }
    }
}

void SplineRasterizer::rasterizeRiverToTile(const RiverData& river, RasterizedTile& tile) const {
    float pixelSize = tile.bounds.width() / tile.resolution;

    for (uint32_t py = 0; py < tile.resolution; py++) {
        for (uint32_t px = 0; px < tile.resolution; px++) {
            // Convert pixel to world position
            glm::vec2 worldPos(
                tile.bounds.min.x + (px + 0.5f) * pixelSize,
                tile.bounds.min.y + (py + 0.5f) * pixelSize
            );

            // Find closest point on this river
            SplineQueryResult best;
            best.distance = std::numeric_limits<float>::max();

            for (const auto& seg : river.segments) {
                // Quick bounds check
                float maxW = std::max(seg.w0, seg.w1);
                if (worldPos.x < seg.bounds.min.x - maxW || worldPos.x > seg.bounds.max.x + maxW ||
                    worldPos.y < seg.bounds.min.y - maxW || worldPos.y > seg.bounds.max.y + maxW) {
                    continue;
                }

                SplineQueryResult result = querySegment(seg, worldPos);
                if (result.distance < best.distance) {
                    best = result;
                }
            }

            if (best.distance < std::numeric_limits<float>::max()) {
                // Compute signed distance (negative = inside riverbed)
                float halfWidth = best.width * 0.5f;
                float sdf = best.distance - halfWidth;

                // Compute alpha with anti-aliased edge
                float alpha = 1.0f - smoothstep(-config.edgeSmoothness, config.edgeSmoothness, sdf);

                if (alpha > 0.001f) {
                    size_t idx = tile.pixelIndex(px, py);

                    // Blend with existing values
                    float existingAlpha = tile.riverbedMask[idx];
                    if (alpha > existingAlpha) {
                        tile.riverbedMask[idx] = alpha;

                        // Compute UV coordinates
                        float u = (best.distance / best.width) + 0.5f;  // 0-1 across width
                        float v = best.t * config.riverUVScale;          // Along length
                        tile.riverbedUVs[idx] = glm::vec2(u, v);
                    }
                }
            }
        }
    }
}

void SplineRasterizer::rasterizeTile(uint32_t tileX, uint32_t tileY, RasterizedTile& outTile) const {
    // Initialize tile
    outTile.tileX = tileX;
    outTile.tileY = tileY;
    outTile.resolution = config.tileResolution;
    outTile.bounds = config.getTileBounds(tileX, tileY);

    size_t numPixels = config.tileResolution * config.tileResolution;

    // Initialize arrays
    outTile.roadMask.assign(numPixels, 0.0f);
    outTile.roadUVs.assign(numPixels, glm::vec2(0.0f));
    outTile.roadTypes.assign(numPixels, static_cast<uint8_t>(RoadGen::RoadType::Lane));

    outTile.riverbedMask.assign(numPixels, 0.0f);
    outTile.riverbedUVs.assign(numPixels, glm::vec2(0.0f));

    // Expand tile bounds for spline intersection test
    TileBounds expandedBounds = outTile.bounds.expanded(50.0f); // 50m margin for wide roads

    // Rasterize rivers first (roads render on top)
    for (const auto& river : riverData) {
        // Check if river intersects expanded tile bounds
        if (river.bounds.max.x < expandedBounds.min.x || river.bounds.min.x > expandedBounds.max.x ||
            river.bounds.max.y < expandedBounds.min.y || river.bounds.min.y > expandedBounds.max.y) {
            continue;
        }
        rasterizeRiverToTile(river, outTile);
    }

    // Rasterize roads
    for (const auto& road : roadData) {
        // Check if road intersects expanded tile bounds
        if (road.bounds.max.x < expandedBounds.min.x || road.bounds.min.x > expandedBounds.max.x ||
            road.bounds.max.y < expandedBounds.min.y || road.bounds.min.y > expandedBounds.max.y) {
            continue;
        }
        rasterizeRoadToTile(road, outTile);
    }
}

} // namespace VirtualTexture
