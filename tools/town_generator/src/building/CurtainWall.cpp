#include <town_generator/building/CurtainWall.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Patch.h>
#include <town_generator/utils/Random.h>
#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <iostream>

namespace town_generator {

CurtainWall::CurtainWall(bool real, Model* model, const std::vector<Patch*>& patches,
                         const std::vector<Point>& reserved)
    : real_(real), patches_(patches) {

    if (patches.size() == 1) {
        shape = patches[0]->shape;
    } else {
        shape = Model::findCircumference(patches);
    }

    segments.resize(shape.size(), true);
    buildGates(real, model, reserved);

    // Smooth wall shape AFTER gate finding (except reserved vertices and gates)
    // This must happen after buildGates because entrance finding uses reference comparison
    // with patch vertices, and smoothing creates new Point objects.
    // Matches the visual intent of Haxe CurtainWall.hx lines 31-36
    if (real && patches.size() > 1) {
        float smoothFactor = std::min(1.0f, 40.0f / static_cast<float>(patches.size()));

        auto isReserved = [&reserved](const Point& v) {
            return std::find_if(reserved.begin(), reserved.end(),
                [&v](const Point& p) { return p.equals(v); }) != reserved.end();
        };

        auto isGate = [this](const Point& v) {
            return std::find_if(gates.begin(), gates.end(),
                [&v](const Point& g) { return g.equals(v); }) != gates.end();
        };

        std::vector<Point> smoothed;
        for (size_t i = 0; i < shape.size(); i++) {
            const Point& v = shape[i];
            if (isReserved(v) || isGate(v)) {
                smoothed.push_back(v);
            } else {
                smoothed.push_back(shape.smoothVertex(v, smoothFactor));
            }
        }
        shape.vertices = smoothed;
    }
}

void CurtainWall::buildGates(bool real, Model* model, const std::vector<Point>& reserved) {
    gates.clear();

    auto isReserved = [&reserved](const Point& v) {
        return std::find_if(reserved.begin(), reserved.end(),
            [&v](const Point& p) { return p.equals(v); }) != reserved.end();
    };

    auto containsVertex = [](const std::vector<Patch*>& patches, const Point& v) {
        int count = 0;
        for (auto* p : patches) {
            if (p->shape.contains(v)) count++;
        }
        return count;
    };

    // Entrances are vertices of the walls with more than 1 adjacent inner ward
    std::vector<Point> entrances;
    if (patches_.size() > 1) {
        for (const auto& v : shape.vertices) {
            if (!isReserved(v) && containsVertex(patches_, v) > 1) {
                entrances.push_back(v);
            }
        }
    } else {
        for (const auto& v : shape.vertices) {
            if (!isReserved(v)) {
                entrances.push_back(v);
            }
        }
    }

    if (entrances.empty()) {
        std::cerr << "CurtainWall: No entrances found!" << std::endl;
        std::cerr << "  patches_.size() = " << patches_.size() << std::endl;
        std::cerr << "  shape.size() = " << shape.size() << std::endl;
        std::cerr << "  reserved.size() = " << reserved.size() << std::endl;
        for (const auto& v : shape.vertices) {
            int count = containsVertex(patches_, v);
            bool isRes = isReserved(v);
            std::cerr << "  v(" << v.x << "," << v.y << ") count=" << count << " reserved=" << isRes << std::endl;
        }
        throw std::runtime_error("Bad walled area shape!");
    }

    do {
        int index = Random::getInt(0, static_cast<int>(entrances.size()));
        Point gate = entrances[index];
        gates.push_back(gate);

        if (real) {
            // Find outer wards adjacent to this gate
            auto patchesAtVertex = model->patchByVertex(gate);
            std::vector<Patch*> outerWards;
            for (auto* w : patchesAtVertex) {
                if (std::find(patches_.begin(), patches_.end(), w) == patches_.end()) {
                    outerWards.push_back(w);
                }
            }

            if (outerWards.size() == 1) {
                // If there is no road leading from the walled patches,
                // we should make one by splitting an outer ward
                Patch* outer = outerWards[0];
                if (outer->shape.size() > 3) {
                    Point wall = shape.next(gate).subtract(shape.prev(gate));
                    Point out(wall.y, -wall.x);

                    // Find farthest point
                    float maxScore = -std::numeric_limits<float>::infinity();
                    Point farthest;
                    for (const auto& v : outer->shape.vertices) {
                        if (shape.contains(v) || isReserved(v)) {
                            continue;
                        }
                        Point dir = v.subtract(gate);
                        float score = dir.dot(out) / dir.length();
                        if (score > maxScore) {
                            maxScore = score;
                            farthest = v;
                        }
                    }

                    if (maxScore > -std::numeric_limits<float>::infinity()) {
                        auto halves = outer->shape.split(gate, farthest);
                        // Replace outer with new patches in model
                        std::vector<Patch*> newPatches;
                        for (auto& half : halves) {
                            newPatches.push_back(new Patch(half.vertices));
                        }
                        model->replacePatches(outer, newPatches);
                    }
                }
            }
        }

        // Removing neighbouring entrances to ensure that no gates are too close
        if (index == 0) {
            if (entrances.size() >= 2) {
                entrances.erase(entrances.begin(), entrances.begin() + 2);
            }
            if (!entrances.empty()) {
                entrances.pop_back();
            }
        } else if (index == static_cast<int>(entrances.size()) - 1) {
            if (index >= 1) {
                entrances.erase(entrances.begin() + index - 1, entrances.end());
            }
            if (!entrances.empty()) {
                entrances.erase(entrances.begin());
            }
        } else {
            if (index >= 1 && index + 2 <= static_cast<int>(entrances.size())) {
                entrances.erase(entrances.begin() + index - 1, entrances.begin() + index + 2);
            }
        }

    } while (entrances.size() >= 3);

    if (gates.empty()) {
        throw std::runtime_error("Bad walled area shape!");
    }

    // Smooth further sections of the wall with gates
    // In Haxe, gate.set() modifies the point in-place, which also modifies the shape
    // since they share the same Point references. We need to update both.
    if (real) {
        for (auto& gate : gates) {
            Point smoothed = shape.smoothVertex(gate);
            // Update the shape vertex to match (Haxe behavior)
            int idx = shape.indexOf(gate);
            if (idx >= 0) {
                shape[idx] = smoothed;
            }
            gate = smoothed;
        }
    }
}

void CurtainWall::buildTowers() {
    towers.clear();
    if (real_) {
        size_t len = shape.size();
        for (size_t i = 0; i < len; i++) {
            const Point& t = shape[i];
            bool isGate = std::find_if(gates.begin(), gates.end(),
                [&t](const Point& g) { return g.equals(t); }) != gates.end();

            if (!isGate && (segments[(i + len - 1) % len] || segments[i])) {
                towers.push_back(t);
            }
        }
    }
}

float CurtainWall::getRadius() const {
    float radius = 0.0f;
    for (const auto& v : shape.vertices) {
        radius = std::max(radius, v.length());
    }
    return radius;
}

bool CurtainWall::bordersBy(Patch* p, const Point& v0, const Point& v1) const {
    bool inPatches = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
    int index = inPatches ?
        shape.findEdge(v0, v1) :
        shape.findEdge(v1, v0);

    if (index != -1 && segments[index]) {
        return true;
    }
    return false;
}

bool CurtainWall::borders(Patch* p) const {
    bool withinWalls = std::find(patches_.begin(), patches_.end(), p) != patches_.end();
    size_t length = shape.size();

    for (size_t i = 0; i < length; i++) {
        if (segments[i]) {
            const Point& v0 = shape[i];
            const Point& v1 = shape[(i + 1) % length];
            int index = withinWalls ?
                p->shape.findEdge(v0, v1) :
                p->shape.findEdge(v1, v0);
            if (index != -1) {
                return true;
            }
        }
    }
    return false;
}

} // namespace town_generator
