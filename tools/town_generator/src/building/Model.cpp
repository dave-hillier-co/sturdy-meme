#include "town_generator/building/Model.h"
#include "town_generator/building/CurtainWall.h"
#include "town_generator/building/Topology.h"
#include "town_generator/wards/Ward.h"
#include "town_generator/wards/Castle.h"
#include "town_generator/wards/Cathedral.h"
#include "town_generator/wards/Market.h"
#include "town_generator/wards/CraftsmenWard.h"
#include "town_generator/wards/MerchantWard.h"
#include "town_generator/wards/PatriciateWard.h"
#include "town_generator/wards/CommonWard.h"
#include "town_generator/wards/AdministrationWard.h"
#include "town_generator/wards/MilitaryWard.h"
#include "town_generator/wards/GateWard.h"
#include "town_generator/wards/Slum.h"
#include "town_generator/wards/Farm.h"
#include "town_generator/wards/Park.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace town_generator {
namespace building {

Model::Model(int nPatches, int seed) : nPatches_(nPatches) {
    utils::Random::reset(seed);

    // Random city features
    plazaNeeded = utils::Random::boolVal(0.8);
    citadelNeeded = utils::Random::boolVal(0.5);
    wallsNeeded = nPatches > 15;
}

Model::~Model() {
    delete citadel;
    delete wall;
}

void Model::build() {
    buildPatches();
    optimizeJunctions();
    buildWalls();
    buildStreets();
    createWards();
    buildGeometry();
}

std::vector<geom::Point> Model::generateRandomPoints(int count, double width, double height) {
    std::vector<geom::Point> points;
    points.reserve(count);

    for (int i = 0; i < count; ++i) {
        points.emplace_back(
            utils::Random::floatVal() * width,
            utils::Random::floatVal() * height
        );
    }

    return points;
}

void Model::buildPatches() {
    // Calculate city size - use grid layout for stability
    int gridSize = static_cast<int>(std::ceil(std::sqrt(nPatches_)));
    double cellSize = 10.0;
    double width = gridSize * cellSize;
    double height = gridSize * cellSize;
    double cityRadius = width * 0.4;

    geom::Point cityCenter(width / 2, height / 2);

    // Create grid-based patches with some randomization
    int patchCount = 0;
    for (int row = 0; row < gridSize && patchCount < nPatches_; ++row) {
        for (int col = 0; col < gridSize && patchCount < nPatches_; ++col) {
            double x = col * cellSize;
            double y = row * cellSize;

            // Add some randomness to the grid
            double jitterX = (utils::Random::floatVal() - 0.5) * cellSize * 0.3;
            double jitterY = (utils::Random::floatVal() - 0.5) * cellSize * 0.3;

            // Create hexagonal-ish patch shape
            auto patch = std::make_unique<Patch>();
            double cx = x + cellSize / 2 + jitterX;
            double cy = y + cellSize / 2 + jitterY;
            double r = cellSize * 0.45;

            // Create irregular hexagon
            int sides = 5 + utils::Random::intVal(0, 3); // 5-7 sides
            for (int i = 0; i < sides; ++i) {
                double angle = (static_cast<double>(i) / sides) * M_PI * 2;
                double rr = r * (0.8 + utils::Random::floatVal() * 0.4);
                patch->shape.push(geom::Point(
                    cx + std::cos(angle) * rr,
                    cy + std::sin(angle) * rr
                ));
            }

            // Mark patches near center as "within city"
            double dist = geom::Point::distance(patch->shape.centroid(), cityCenter);
            patch->withinCity = dist < cityRadius;
            patch->withinWalls = patch->withinCity && wallsNeeded;

            patches.push_back(patch.get());
            ownedPatches_.push_back(std::move(patch));
            patchCount++;
        }
    }

    // Create border patch (the bounding area)
    border.shape = geom::Polygon::rect(width, height);
    border.shape.offset(cityCenter);
}

void Model::optimizeJunctions() {
    // Simplify patches by removing very short edges
    for (auto* patch : patches) {
        if (patch->shape.length() > 4) {
            patch->shape = patch->shape.filterShort(0.1);
        }
    }
}

void Model::buildWalls() {
    // Simplified: just assign all patches to inner, skip complex wall building
    inner = patches;
    for (auto* p : patches) {
        p->withinCity = true;
    }

    // Skip complex wall building - requires proper shared-vertex topology
    // TODO: Implement proper wall building once Voronoi tessellation is working
    wallsNeeded = false;
    citadelNeeded = false;
}

void Model::buildStreets() {
    // Skip street building for now - requires proper shared-vertex topology
    // TODO: Implement proper street network once Voronoi tessellation is working
}

void Model::buildRoads() {
    // Roads outside walls - not implemented in basic version
}

std::vector<Patch*> Model::patchByVertex(const geom::Point& v) {
    std::vector<Patch*> result;
    for (auto* p : patches) {
        if (p->shape.contains(v)) {
            result.push_back(p);
        }
    }
    return result;
}

geom::Polygon Model::findCircumference(const std::vector<Patch*>& patches) {
    if (patches.empty()) return geom::Polygon();
    if (patches.size() == 1) return patches[0]->shape;

    // Find all edges that belong to exactly one patch
    std::vector<std::pair<geom::Point, geom::Point>> boundaryEdges;

    for (auto* patch : patches) {
        patch->shape.forEdge([&patches, patch, &boundaryEdges](
            const geom::Point& v0, const geom::Point& v1
        ) {
            // Check if this edge is shared with another patch in the set
            bool isShared = false;
            for (auto* other : patches) {
                if (other == patch) continue;
                if (other->shape.findEdge(v1, v0) != -1) {
                    isShared = true;
                    break;
                }
            }

            if (!isShared) {
                boundaryEdges.push_back({v0, v1});
            }
        });
    }

    if (boundaryEdges.empty()) return geom::Polygon();

    // Chain edges together
    geom::Polygon result;
    result.push(boundaryEdges[0].first);

    geom::Point current = boundaryEdges[0].second;
    boundaryEdges.erase(boundaryEdges.begin());

    while (!boundaryEdges.empty()) {
        result.push(current);

        bool found = false;
        for (auto it = boundaryEdges.begin(); it != boundaryEdges.end(); ++it) {
            if (it->first == current) {
                current = it->second;
                boundaryEdges.erase(it);
                found = true;
                break;
            }
        }

        if (!found) break;
    }

    return result;
}

void Model::createWards() {
    // Ward type distribution
    std::vector<std::function<wards::Ward*()>> wardTypes = {
        []() { return new wards::CraftsmenWard(); },
        []() { return new wards::MerchantWard(); },
        []() { return new wards::CommonWard(); },
        []() { return new wards::Slum(); },
        []() { return new wards::PatriciateWard(); },
        []() { return new wards::AdministrationWard(); },
        []() { return new wards::MilitaryWard(); },
    };

    std::vector<double> weights = {3, 2, 4, 2, 1, 1, 1};

    // Assign wards to patches
    for (auto* patch : patches) {
        wards::Ward* ward = nullptr;

        // Special wards
        if (patch->withinCity && !patch->ward) {
            if (citadel && patch == patches[0]) {
                ward = new wards::Castle();
            } else if (plazaNeeded && !plaza && patch->withinWalls) {
                plaza = patch;
                ward = new wards::Market();
            } else if (utils::Random::boolVal(0.1) && patch->withinWalls) {
                ward = new wards::Cathedral();
            }
        }

        // Regular wards
        if (!ward) {
            if (patch->withinCity) {
                // Weighted random selection
                double total = 0;
                for (double w : weights) total += w;

                double r = utils::Random::floatVal() * total;
                double acc = 0;
                for (size_t i = 0; i < wardTypes.size(); ++i) {
                    acc += weights[i];
                    if (r <= acc) {
                        ward = wardTypes[i]();
                        break;
                    }
                }

                if (!ward) ward = new wards::CommonWard();
            } else {
                // Outer patches are farms or parks
                if (utils::Random::boolVal(0.7)) {
                    ward = new wards::Farm();
                } else {
                    ward = new wards::Park();
                }
            }
        }

        if (ward) {
            ward->patch = patch;
            ward->model = this;
            patch->ward = ward;
            wards_.emplace_back(ward);
        }
    }
}

void Model::buildGeometry() {
    for (const auto& ward : wards_) {
        ward->createGeometry();
    }
}

} // namespace building
} // namespace town_generator
