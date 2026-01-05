#include "town_generator/building/Canal.h"
#include "town_generator/building/Model.h"
#include "town_generator/building/Patch.h"
#include "town_generator/geom/GeomUtils.h"
#include "town_generator/utils/Random.h"
#include <algorithm>
#include <cmath>

namespace town_generator {
namespace building {

std::unique_ptr<Canal> Canal::createRiver(Model* model) {
    if (!model) return nullptr;

    auto canal = std::make_unique<Canal>();
    canal->model = model;

    // River width based on city size
    canal->width = 3.0 + utils::Random::floatVal() * 3.0;

    // Find shore patches to start the river
    std::vector<Patch*> shorePatches;
    for (auto* patch : model->patches) {
        if (patch->waterbody) continue;

        // Check if patch borders water
        for (auto* neighbor : patch->neighbors) {
            if (neighbor->waterbody) {
                shorePatches.push_back(patch);
                break;
            }
        }
    }

    if (shorePatches.empty()) {
        // No shore patches - river not possible
        return nullptr;
    }

    // Pick a random shore patch as start
    size_t startIdx = static_cast<size_t>(utils::Random::floatVal() * shorePatches.size());
    if (startIdx >= shorePatches.size()) startIdx = shorePatches.size() - 1;

    Patch* startPatch = shorePatches[startIdx];

    // Find the shore edge on the start patch
    geom::Point shorePoint;
    bool foundShore = false;
    for (size_t i = 0; i < startPatch->shape.length(); ++i) {
        const geom::Point& v0 = startPatch->shape[i];
        const geom::Point& v1 = startPatch->shape[(i + 1) % startPatch->shape.length()];

        for (auto* neighbor : startPatch->neighbors) {
            if (neighbor->waterbody &&
                neighbor->shape.contains(v0) && neighbor->shape.contains(v1)) {
                // This edge borders water
                shorePoint = geom::GeomUtils::interpolate(v0, v1, 0.5);
                foundShore = true;
                break;
            }
        }
        if (foundShore) break;
    }

    if (!foundShore) {
        return nullptr;
    }

    // Build course from shore toward city center
    geom::Point endPoint = model->plaza
        ? model->plaza->shape.centroid()
        : model->patches[0]->shape.centroid();

    canal->buildCourse(shorePoint, endPoint);
    canal->findBridges();

    return canal;
}

void Canal::buildCourse(const geom::Point& start, const geom::Point& end) {
    course.clear();

    // Simple straight canal with some randomness
    course.push_back(start);

    geom::Point dir = end.subtract(start);
    double dist = dir.length();
    if (dist < 1) {
        course.push_back(end);
        return;
    }

    // Add intermediate points with some waviness
    int numPoints = static_cast<int>(dist / 20.0);
    if (numPoints < 1) numPoints = 1;
    if (numPoints > 10) numPoints = 10;

    geom::Point perpDir(-dir.y / dist, dir.x / dist);

    for (int i = 1; i < numPoints; ++i) {
        double t = static_cast<double>(i) / numPoints;
        geom::Point basePoint = start.add(dir.scale(t));

        // Add some waviness
        double waveAmplitude = width * 0.5 * (utils::Random::floatVal() - 0.5);
        geom::Point waveOffset = perpDir.scale(waveAmplitude);

        course.push_back(basePoint.add(waveOffset));
    }

    course.push_back(end);
}

void Canal::findBridges() {
    if (!model || course.size() < 2) return;

    bridges.clear();

    // Find street crossings
    for (const auto& artery : model->arteries) {
        if (artery.size() < 2) continue;

        for (size_t i = 0; i + 1 < artery.size(); ++i) {
            geom::Point streetP1 = *artery[i];
            geom::Point streetP2 = *artery[i + 1];

            // Check if this street segment crosses any canal segment
            for (size_t j = 0; j + 1 < course.size(); ++j) {
                const geom::Point& canalP1 = course[j];
                const geom::Point& canalP2 = course[j + 1];

                // Check for intersection
                auto intersection = geom::GeomUtils::intersectLines(
                    canalP1.x, canalP1.y,
                    canalP2.x - canalP1.x, canalP2.y - canalP1.y,
                    streetP1.x, streetP1.y,
                    streetP2.x - streetP1.x, streetP2.y - streetP1.y
                );

                if (intersection) {
                    double t1 = intersection->x;
                    double t2 = intersection->y;

                    // Check if intersection is within both segments
                    if (t1 >= 0 && t1 <= 1 && t2 >= 0 && t2 <= 1) {
                        geom::Point bridgePoint(
                            canalP1.x + t1 * (canalP2.x - canalP1.x),
                            canalP1.y + t1 * (canalP2.y - canalP1.y)
                        );
                        geom::Point streetDir = streetP2.subtract(streetP1);
                        bridges[bridgePoint] = streetDir.norm(1.0);
                    }
                }
            }
        }
    }
}

geom::Polygon Canal::getWaterPolygon() const {
    if (course.size() < 2) {
        return geom::Polygon();
    }

    // Create a polygon by offsetting the course on both sides
    std::vector<geom::Point> leftSide;
    std::vector<geom::Point> rightSide;

    double halfWidth = width / 2.0;

    for (size_t i = 0; i < course.size(); ++i) {
        geom::Point dir;
        if (i == 0) {
            dir = course[1].subtract(course[0]);
        } else if (i == course.size() - 1) {
            dir = course[i].subtract(course[i - 1]);
        } else {
            dir = course[i + 1].subtract(course[i - 1]);
        }

        double len = dir.length();
        if (len < 0.001) continue;

        geom::Point perpDir(-dir.y / len, dir.x / len);

        leftSide.push_back(course[i].add(perpDir.scale(halfWidth)));
        rightSide.push_back(course[i].add(perpDir.scale(-halfWidth)));
    }

    // Combine into polygon (left side forward, right side backward)
    std::vector<geom::Point> polyPoints;
    for (const auto& p : leftSide) {
        polyPoints.push_back(p);
    }
    for (auto it = rightSide.rbegin(); it != rightSide.rend(); ++it) {
        polyPoints.push_back(*it);
    }

    return geom::Polygon(polyPoints);
}

} // namespace building
} // namespace town_generator
