#include "lakes.h"
#include <SDL3/SDL_log.h>
#include <queue>
#include <map>
#include <algorithm>
#include <cmath>

namespace {

// Priority-flood depression filling (Barnes et al.): compute, for every
// cell, the level water would pool to before it can drain off the grid.
// Seeded from the border; each popped cell propagates
// fill = max(elevation, fill(source)) to unvisited neighbors.
std::vector<uint16_t> priority_flood_fill(const ElevationGrid& elevation) {
    const int w = elevation.width;
    const int h = elevation.height;
    std::vector<uint16_t> fill(static_cast<size_t>(w) * h, 0);
    std::vector<uint8_t> visited(static_cast<size_t>(w) * h, 0);

    struct Cell {
        uint16_t level;
        int x, y;
        bool operator>(const Cell& o) const { return level > o.level; }
    };
    std::priority_queue<Cell, std::vector<Cell>, std::greater<Cell>> pq;

    auto push = [&](int x, int y, uint16_t level) {
        size_t idx = static_cast<size_t>(y) * w + x;
        if (visited[idx]) return;
        visited[idx] = 1;
        uint16_t lvl = std::max(level, elevation.at(x, y));
        fill[idx] = lvl;
        pq.push({lvl, x, y});
    };

    for (int x = 0; x < w; ++x) {
        push(x, 0, 0);
        push(x, h - 1, 0);
    }
    for (int y = 0; y < h; ++y) {
        push(0, y, 0);
        push(w - 1, y, 0);
    }

    static const int dx8[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int dy8[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

    while (!pq.empty()) {
        Cell c = pq.top();
        pq.pop();
        for (int d = 0; d < 8; ++d) {
            int nx = c.x + dx8[d];
            int ny = c.y + dy8[d];
            if (!elevation.in_bounds(nx, ny)) continue;
            push(nx, ny, c.level);
        }
    }
    return fill;
}

// Trace the outer boundary of a labeled component as a closed cell-edge
// contour. Boundary edges are the unit edges between an inside cell and an
// outside cell, directed so the inside is on the left; chaining them yields
// counter-clockwise outer rings (holes are ignored - the largest ring wins).
std::vector<std::pair<float, float>> trace_boundary(
    const std::vector<uint32_t>& labels, int w, int h, uint32_t label) {
    // Directed edge between grid vertices, keyed by start vertex.
    struct Edge {
        int x0, y0, x1, y1;
        bool used = false;
    };
    std::vector<Edge> edges;
    auto inside = [&](int x, int y) {
        return x >= 0 && x < w && y >= 0 && y < h &&
               labels[static_cast<size_t>(y) * w + x] == label;
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!inside(x, y)) continue;
            // For each side with an outside neighbor, add the directed edge
            // that keeps the cell on the left.
            if (!inside(x, y - 1)) edges.push_back({x, y, x + 1, y});         // top: left->right
            if (!inside(x + 1, y)) edges.push_back({x + 1, y, x + 1, y + 1}); // right: top->bottom
            if (!inside(x, y + 1)) edges.push_back({x + 1, y + 1, x, y + 1}); // bottom: right->left
            if (!inside(x - 1, y)) edges.push_back({x, y + 1, x, y});         // left: bottom->top
        }
    }
    if (edges.empty()) return {};

    std::multimap<std::pair<int, int>, size_t> byStart;
    for (size_t i = 0; i < edges.size(); ++i) {
        byStart.emplace(std::make_pair(edges[i].x0, edges[i].y0), i);
    }

    std::vector<std::pair<float, float>> best;
    for (size_t start = 0; start < edges.size(); ++start) {
        if (edges[start].used) continue;
        std::vector<std::pair<float, float>> ring;
        size_t cur = start;
        while (!edges[cur].used) {
            edges[cur].used = true;
            ring.emplace_back(static_cast<float>(edges[cur].x0),
                              static_cast<float>(edges[cur].y0));
            auto key = std::make_pair(edges[cur].x1, edges[cur].y1);
            auto range = byStart.equal_range(key);
            size_t next = SIZE_MAX;
            // Prefer the leftmost turn relative to the incoming direction so
            // diagonal-touching cells stay on a consistent contour.
            int inDx = edges[cur].x1 - edges[cur].x0;
            int inDy = edges[cur].y1 - edges[cur].y0;
            int bestTurn = -3;
            for (auto it = range.first; it != range.second; ++it) {
                const Edge& e = edges[it->second];
                if (e.used) continue;
                int outDx = e.x1 - e.x0;
                int outDy = e.y1 - e.y0;
                // Cross product: positive = left turn, negative = right turn
                int cross = inDx * outDy - inDy * outDx;
                int turn = (cross > 0) ? 1 : (cross < 0 ? -1 : 0);
                if (turn > bestTurn) {
                    bestTurn = turn;
                    next = it->second;
                }
            }
            if (next == SIZE_MAX) break;
            cur = next;
        }
        if (ring.size() >= 4 && ring.size() > best.size()) {
            best = std::move(ring);
        }
    }

    // Drop collinear intermediate points to keep the polygon compact.
    if (best.size() > 4) {
        std::vector<std::pair<float, float>> simplified;
        simplified.reserve(best.size());
        const size_t n = best.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& prev = best[(i + n - 1) % n];
            const auto& curP = best[i];
            const auto& nextP = best[(i + 1) % n];
            float cross = (curP.first - prev.first) * (nextP.second - curP.second) -
                          (curP.second - prev.second) * (nextP.first - curP.first);
            if (std::abs(cross) > 1e-6f) {
                simplified.push_back(curP);
            }
        }
        if (simplified.size() >= 3) {
            best = std::move(simplified);
        }
    }

    if (!best.empty()) {
        best.push_back(best.front()); // close the ring
    }
    return best;
}

} // namespace

std::vector<Lake> extract_lakes(
    const ElevationGrid& elevation,
    uint16_t sea_level,
    uint32_t min_area_cells
) {
    const int w = elevation.width;
    const int h = elevation.height;
    std::vector<uint16_t> fill = priority_flood_fill(elevation);

    // Lake mask: cells that pool above their elevation, with a water surface
    // above sea level (submarine depressions are sea floor, not lakes).
    std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
    for (size_t i = 0; i < mask.size(); ++i) {
        if (fill[i] > elevation.data[i] && fill[i] > sea_level) {
            mask[i] = 1;
        }
    }

    // 4-connected component labeling over the mask.
    std::vector<uint32_t> labels(static_cast<size_t>(w) * h, 0);
    uint32_t next_label = 0;
    std::vector<Lake> lakes;

    static const int dx4[4] = {1, -1, 0, 0};
    static const int dy4[4] = {0, 0, 1, -1};

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(y) * w + x;
            if (!mask[idx] || labels[idx] != 0) continue;

            ++next_label;
            Lake lake;
            lake.fill_level = fill[idx];
            lake.min_elevation = elevation.data[idx];
            double sumX = 0, sumY = 0;

            std::queue<std::pair<int, int>> bfs;
            bfs.push({x, y});
            labels[idx] = next_label;
            while (!bfs.empty()) {
                auto [cx, cy] = bfs.front();
                bfs.pop();
                size_t cidx = static_cast<size_t>(cy) * w + cx;
                ++lake.area_cells;
                sumX += cx + 0.5;
                sumY += cy + 0.5;
                lake.fill_level = std::max(lake.fill_level, fill[cidx]);
                lake.min_elevation = std::min(lake.min_elevation, elevation.data[cidx]);
                for (int d = 0; d < 4; ++d) {
                    int nx = cx + dx4[d];
                    int ny = cy + dy4[d];
                    if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    size_t nidx = static_cast<size_t>(ny) * w + nx;
                    if (mask[nidx] && labels[nidx] == 0) {
                        labels[nidx] = next_label;
                        bfs.push({nx, ny});
                    }
                }
            }

            if (lake.area_cells < min_area_cells) continue;

            lake.centroid_x = static_cast<float>(sumX / lake.area_cells);
            lake.centroid_y = static_cast<float>(sumY / lake.area_cells);
            lake.boundary = trace_boundary(labels, w, h, next_label);
            if (lake.boundary.size() < 4) continue;

            lakes.push_back(std::move(lake));
        }
    }

    SDL_Log("Lake extraction: %zu lakes (min area %u cells, sea level %u)",
            lakes.size(), min_area_cells, sea_level);
    return lakes;
}
