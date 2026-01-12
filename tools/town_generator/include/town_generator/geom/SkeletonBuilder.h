#pragma once

#include "town_generator/geom/Point.h"
#include "town_generator/geom/GeomUtils.h"
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <limits>
#include <algorithm>

namespace town_generator {
namespace geom {

// Forward declarations
class Rib;

/**
 * Node in the skeleton tree
 */
class SkeletonNode {
public:
    Point point;
    double height;
    Rib* child1;
    Rib* child2;
    Rib* parent;

    SkeletonNode(const Point& p, double h = 0, Rib* c1 = nullptr, Rib* c2 = nullptr)
        : point(p), height(h), child1(c1), child2(c2), parent(nullptr) {}
};

/**
 * Segment of the polygon boundary
 */
class SkeletonSegment {
public:
    Point p0;
    Point p1;
    Point dir;
    double len;
    Rib* lRib;
    Rib* rRib;

    SkeletonSegment(const Point& start, const Point& end)
        : p0(start)
        , p1(end)
        , dir(end.subtract(start).norm())
        , len(Point::distance(start, end))
        , lRib(nullptr)
        , rRib(nullptr)
    {}
};

/**
 * Rib of the skeleton (an edge from a node along a bisector)
 */
class Rib {
public:
    SkeletonNode* a;
    SkeletonNode* b;
    SkeletonSegment* left;
    SkeletonSegment* right;
    Point slope;

    Rib(SkeletonNode* node, SkeletonSegment* leftSeg, SkeletonSegment* rightSeg)
        : a(node)
        , b(nullptr)
        , left(leftSeg)
        , right(rightSeg)
    {
        a->parent = this;
        left->lRib = this;
        right->rRib = this;

        // Calculate bisector direction (slope)
        const Point& leftDir = left->dir;
        const Point& rightDir = right->dir;
        double dot = leftDir.x * rightDir.x + leftDir.y * rightDir.y;

        if (dot > 0.99999) {
            // Nearly parallel - perpendicular bisector
            slope = Point(-leftDir.y, leftDir.x);
        } else {
            double c = std::sqrt((1 + dot) / 2);
            slope = rightDir.subtract(leftDir);
            slope = slope.norm().scale(1 / c);

            // Ensure correct orientation for reflex vertices
            if (!a->child1) {
                double cross = leftDir.x * rightDir.y - leftDir.y * rightDir.x;
                if (cross < 0) {
                    slope = slope.scale(-1);
                }
            }
        }
    }
};

/**
 * SkeletonBuilder - Straight skeleton algorithm
 *
 * Generates a straight skeleton for polygon roof generation.
 * The skeleton consists of ribs (edges) and bones (the skeleton edges).
 *
 * Faithful port from mfcg-clean/geometry/SkeletonBuilder.js
 */
class SkeletonBuilder {
public:
    double height;
    std::vector<Point> poly;
    std::vector<std::unique_ptr<SkeletonSegment>> segments;
    std::map<std::string, std::unique_ptr<SkeletonNode>> leaves;
    std::vector<std::unique_ptr<Rib>> allRibs;  // Owns all ribs
    std::vector<Rib*> ribs;  // Active ribs
    std::vector<Rib*> bones;  // Completed skeleton ribs
    Rib* root;
    std::vector<SkeletonSegment*> gables;

    /**
     * Create a skeleton builder
     * @param polygon Input polygon
     * @param autoRun Automatically build skeleton
     */
    explicit SkeletonBuilder(const std::vector<Point>& polygon, bool autoRun = false)
        : height(0)
        , poly(polygon)
        , root(nullptr)
    {
        size_t n = polygon.size();

        // Create segments
        for (size_t i = 0; i < n; ++i) {
            segments.push_back(std::make_unique<SkeletonSegment>(
                polygon[i],
                polygon[(i + 1) % n]
            ));
        }

        // Create leaf nodes and initial ribs
        for (size_t i = 0; i < n; ++i) {
            auto node = std::make_unique<SkeletonNode>(polygon[i]);
            SkeletonNode* nodePtr = node.get();
            leaves[pointKey(polygon[i])] = std::move(node);

            auto rib = std::make_unique<Rib>(
                nodePtr,
                segments[i].get(),
                segments[(i + n - 1) % n].get()
            );
            ribs.push_back(rib.get());
            allRibs.push_back(std::move(rib));
        }

        if (autoRun) {
            run();
        }
    }

    /**
     * Find intersection of two ribs
     * @return Intersection parameters (x=t for a, y=t for b)
     */
    static std::optional<Point> intersect(Rib* a, Rib* b) {
        return GeomUtils::intersectLines(
            a->a->point.x, a->a->point.y, a->slope.x, a->slope.y,
            b->a->point.x, b->a->point.y, b->slope.x, b->slope.y
        );
    }

    /**
     * Run the skeleton algorithm to completion
     */
    void run() {
        while (step()) {}
    }

    /**
     * Perform one step of the algorithm
     * @return True if more steps needed
     */
    bool step() {
        if (ribs.size() <= 2) {
            if (ribs.size() == 2) {
                root = ribs[0];
                root->b = ribs[1]->a;
                root->b->parent = root;
                bones.push_back(root);
                ribs.clear();
            }
            return false;
        }

        // Find earliest event
        double minHeight = std::numeric_limits<double>::infinity();
        Rib* bestRib1 = nullptr;
        Rib* bestRib2 = nullptr;
        std::optional<Point> bestIntersection;

        for (Rib* rib : ribs) {
            // Check intersection with right neighbor
            std::optional<Point> candidate;
            Rib* candidateRib = nullptr;
            double candidateT = std::numeric_limits<double>::infinity();

            Rib* rightNeighbor = rib->right->lRib;
            auto rightInt = intersect(rib, rightNeighbor);
            if (rightInt && rightInt->x >= 0 && rightInt->y >= 0) {
                double h = rightInt->x + rib->a->height;
                if (h < candidateT) {
                    candidateT = h;
                    candidate = rightInt;
                    candidateRib = rightNeighbor;
                }
            }

            // Check intersection with left neighbor
            Rib* leftNeighbor = rib->left->rRib;
            auto leftInt = intersect(rib, leftNeighbor);
            if (leftInt && leftInt->x >= 0 && leftInt->y >= 0) {
                double h = leftInt->x + rib->a->height;
                if (h < candidateT) {
                    candidateT = h;
                    candidate = leftInt;
                    candidateRib = leftNeighbor;
                }
            }

            if (candidate && candidateRib) {
                double eventHeight = candidate->y + candidateRib->a->height;
                if (eventHeight < minHeight) {
                    minHeight = eventHeight;
                    bestRib1 = rib;
                    bestRib2 = candidateRib;
                    bestIntersection = candidate;
                }
            }
        }

        if (!bestIntersection) return false;

        height = minHeight;

        // Calculate intersection point
        double t = bestIntersection->x;
        Point intersectPoint(
            bestRib1->a->point.x + bestRib1->slope.x * t,
            bestRib1->a->point.y + bestRib1->slope.y * t
        );

        merge(bestRib1, bestRib2, intersectPoint);
        return true;
    }

    /**
     * Merge two ribs at an intersection point
     */
    Rib* merge(Rib* rib1, Rib* rib2, const Point& point) {
        auto node = std::make_unique<SkeletonNode>(point, height, rib1, rib2);
        SkeletonNode* nodePtr = node.get();
        leaves[pointKey(point)] = std::move(node);

        rib1->b = nodePtr;
        rib2->b = nodePtr;

        std::unique_ptr<Rib> newRib;
        if (rib1->right == rib2->left) {
            newRib = std::make_unique<Rib>(nodePtr, rib1->left, rib2->right);
        } else {
            newRib = std::make_unique<Rib>(nodePtr, rib2->left, rib1->right);
        }

        Rib* newRibPtr = newRib.get();
        ribs.push_back(newRibPtr);
        allRibs.push_back(std::move(newRib));

        // Add to bones and remove from active ribs
        bones.push_back(rib1);
        ribs.erase(std::remove(ribs.begin(), ribs.end(), rib1), ribs.end());

        bones.push_back(rib2);
        ribs.erase(std::remove(ribs.begin(), ribs.end(), rib2), ribs.end());

        return newRibPtr;
    }

    /**
     * Add gables (horizontal roof edges on longest sides)
     */
    void addGables() {
        gables.clear();

        for (auto& segment : segments) {
            auto it1 = leaves.find(pointKey(segment->p0));
            auto it2 = leaves.find(pointKey(segment->p1));

            if (it1 == leaves.end() || it2 == leaves.end()) continue;

            SkeletonNode* node1 = it1->second.get();
            SkeletonNode* node2 = it2->second.get();

            if (!node1->parent || !node2->parent) continue;

            Rib* rib1 = node1->parent;
            Rib* rib2 = node2->parent;

            if (rib1->b == rib2->b) {
                SkeletonNode* apex = rib1->b;
                Point& apexPoint = apex->point;

                // Find the sibling rib
                Rib* siblingRib = nullptr;
                if (rib1 == root) {
                    siblingRib = (rib2 == apex->child1) ? apex->child2 : apex->child1;
                } else if (rib2 == root) {
                    siblingRib = (rib1 == apex->child1) ? apex->child2 : apex->child1;
                } else if (apex->parent) {
                    siblingRib = apex->parent;
                } else {
                    continue;
                }

                if (!siblingRib) continue;

                auto intersection = GeomUtils::intersectLines(
                    segment->p0.x, segment->p0.y, segment->dir.x, segment->dir.y,
                    apexPoint.x, apexPoint.y, siblingRib->slope.x, siblingRib->slope.y
                );

                if (intersection && intersection->x > 0 && intersection->x < segment->len) {
                    Point gablePoint(
                        segment->p0.x + segment->dir.x * intersection->x,
                        segment->p0.y + segment->dir.y * intersection->x
                    );
                    apexPoint.x = gablePoint.x;
                    apexPoint.y = gablePoint.y;
                    gables.push_back(segment.get());
                }
            }
        }
    }

    /**
     * Get path between two leaf nodes through the skeleton
     */
    std::vector<SkeletonNode*> getPath(const Point& p1, const Point& p2) {
        auto it1 = leaves.find(pointKey(p1));
        auto it2 = leaves.find(pointKey(p2));

        if (it1 == leaves.end() || it2 == leaves.end()) return {};

        SkeletonNode* node1 = it1->second.get();
        SkeletonNode* node2 = it2->second.get();

        if (node1 == node2) return {node1};

        auto path1 = getPathToRoot(node1);
        auto path2 = getPathToRoot(node2);

        // Find common ancestor
        if (!path1.empty() && !path2.empty() && path1.back() == path2.back()) {
            while (!path1.empty() && !path2.empty() && path1.back() == path2.back()) {
                path1.pop_back();
                path2.pop_back();
            }
            if (!path1.empty() && path1.back()->parent) {
                path1.push_back(path1.back()->parent->b);
            }
        }

        // Combine paths
        std::reverse(path2.begin(), path2.end());
        path1.insert(path1.end(), path2.begin(), path2.end());

        return path1;
    }

    /**
     * Get path from a node to the root
     */
    std::vector<SkeletonNode*> getPathToRoot(SkeletonNode* node) {
        std::vector<SkeletonNode*> path = {node};

        while (root && root->a != node && root->b != node) {
            if (!node->parent || !node->parent->b) break;
            node = node->parent->b;
            path.push_back(node);
        }

        return path;
    }

    /**
     * Get all skeleton edges as line segments
     * Useful for visualization/export
     */
    std::vector<std::pair<Point, Point>> getSkeletonEdges() const {
        std::vector<std::pair<Point, Point>> edges;
        for (Rib* bone : bones) {
            if (bone->a && bone->b) {
                edges.push_back({bone->a->point, bone->b->point});
            }
        }
        return edges;
    }

private:
    static std::string pointKey(const Point& p) {
        // Use fixed precision for consistent keys
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.6f,%.6f", p.x, p.y);
        return std::string(buf);
    }
};

} // namespace geom
} // namespace town_generator
