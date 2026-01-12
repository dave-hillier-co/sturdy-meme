#pragma once

#include "town_generator/geom/Point.h"
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

namespace town_generator {
namespace geom {

// Forward declarations
class HalfEdge;
class Face;

/**
 * Vertex in the DCEL
 */
class Vertex {
public:
    Point point;
    std::vector<HalfEdge*> edges;  // All half-edges originating from this vertex

    explicit Vertex(const Point& p) : point(p) {}

    double x() const { return point.x; }
    double y() const { return point.y; }
};

/**
 * HalfEdge in the DCEL
 */
class HalfEdge {
public:
    Vertex* origin = nullptr;
    HalfEdge* twin = nullptr;
    HalfEdge* next = nullptr;
    HalfEdge* prev = nullptr;
    Face* face = nullptr;
    void* data = nullptr;  // Custom data (e.g., edge type)

    HalfEdge() = default;

    /**
     * Get destination vertex
     */
    Vertex* destination() const {
        return next ? next->origin : nullptr;
    }

    /**
     * Get length of edge
     */
    double length() const {
        if (!origin || !destination()) return 0;
        return Point::distance(origin->point, destination()->point);
    }
};

/**
 * Face in the DCEL
 */
class Face {
public:
    HalfEdge* halfEdge = nullptr;  // One half-edge on the boundary
    void* data = nullptr;           // Custom data (e.g., Cell)

    Face() = default;

    /**
     * Get polygon for this face
     */
    std::vector<Point> getPoly() const {
        std::vector<Point> poly;
        HalfEdge* edge = halfEdge;
        if (!edge) return poly;

        do {
            poly.push_back(edge->origin->point);
            edge = edge->next;
        } while (edge && edge != halfEdge);

        return poly;
    }

    /**
     * Iterate over edges of this face
     */
    template<typename Func>
    void forEachEdge(Func f) const {
        HalfEdge* edge = halfEdge;
        if (!edge) return;

        do {
            f(edge);
            edge = edge->next;
        } while (edge && edge != halfEdge);
    }

    /**
     * Get all edges as a vector
     */
    std::vector<HalfEdge*> edges() const {
        std::vector<HalfEdge*> result;
        forEachEdge([&result](HalfEdge* e) {
            result.push_back(e);
        });
        return result;
    }

    /**
     * Get all vertices of this face
     */
    std::vector<Vertex*> vertices() const {
        std::vector<Vertex*> verts;
        forEachEdge([&verts](HalfEdge* e) {
            verts.push_back(e->origin);
        });
        return verts;
    }
};

/**
 * Doubly Connected Edge List
 *
 * A topological data structure for representing planar subdivisions.
 * Used for Voronoi diagrams and city patch topology.
 *
 * Faithful port from mfcg-clean/geometry/DCEL.js
 */
class DCEL {
private:
    // Helper to create edge key for twin lookup
    static std::string edgeKey(const Point& p1, const Point& p2) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6)
            << p1.x << "," << p1.y << "->" << p2.x << "," << p2.y;
        return oss.str();
    }

public:
    std::map<std::string, std::unique_ptr<Vertex>> vertices;
    std::vector<std::unique_ptr<HalfEdge>> edges;
    std::vector<std::unique_ptr<Face>> faces;

    DCEL() = default;

    explicit DCEL(const std::vector<std::vector<Point>>& polygons) {
        if (!polygons.empty()) {
            buildFromPolygons(polygons);
        }
    }

    /**
     * Build DCEL from array of polygons
     */
    void buildFromPolygons(const std::vector<std::vector<Point>>& polygons) {
        // Create vertices
        for (const auto& poly : polygons) {
            for (const auto& point : poly) {
                std::string key = pointKey(point);
                if (vertices.find(key) == vertices.end()) {
                    vertices[key] = std::make_unique<Vertex>(point);
                }
            }
        }

        // Map for finding twin edges
        std::map<std::string, HalfEdge*> edgeMap;

        // Create faces and edges
        for (const auto& poly : polygons) {
            auto face = std::make_unique<Face>();
            Face* facePtr = face.get();
            faces.push_back(std::move(face));

            std::vector<HalfEdge*> faceEdges;
            size_t n = poly.size();

            for (size_t i = 0; i < n; ++i) {
                const Point& p1 = poly[i];
                const Point& p2 = poly[(i + 1) % n];

                auto edge = std::make_unique<HalfEdge>();
                HalfEdge* edgePtr = edge.get();

                edgePtr->origin = getVertex(p1);
                edgePtr->origin->edges.push_back(edgePtr);
                edgePtr->face = facePtr;

                faceEdges.push_back(edgePtr);
                edges.push_back(std::move(edge));

                // Store for twin lookup
                edgeMap[edgeKey(p1, p2)] = edgePtr;
            }

            // Link edges in cycle
            for (size_t i = 0; i < faceEdges.size(); ++i) {
                faceEdges[i]->next = faceEdges[(i + 1) % faceEdges.size()];
                faceEdges[i]->prev = faceEdges[(i + faceEdges.size() - 1) % faceEdges.size()];
            }

            facePtr->halfEdge = faceEdges[0];
        }

        // Link twins
        for (const auto& poly : polygons) {
            size_t n = poly.size();
            for (size_t i = 0; i < n; ++i) {
                const Point& p1 = poly[i];
                const Point& p2 = poly[(i + 1) % n];

                auto it1 = edgeMap.find(edgeKey(p1, p2));
                auto it2 = edgeMap.find(edgeKey(p2, p1));

                if (it1 != edgeMap.end() && it2 != edgeMap.end()) {
                    it1->second->twin = it2->second;
                    it2->second->twin = it1->second;
                }
            }
        }
    }

    /**
     * Get vertex for point (or create if not exists)
     */
    Vertex* getVertex(const Point& point) {
        std::string key = pointKey(point);
        auto it = vertices.find(key);
        if (it == vertices.end()) {
            vertices[key] = std::make_unique<Vertex>(point);
            return vertices[key].get();
        }
        return it->second.get();
    }

    /**
     * Find circumference edges of a set of faces
     */
    static std::vector<HalfEdge*> circumference(HalfEdge* startEdge, const std::vector<Face*>& faceList) {
        std::set<Face*> faceSet(faceList.begin(), faceList.end());
        std::vector<HalfEdge*> boundaryEdges;

        for (Face* face : faceList) {
            face->forEachEdge([&](HalfEdge* edge) {
                // Edge is on boundary if twin is null or twin's face is not in set
                if (!edge->twin || faceSet.find(edge->twin->face) == faceSet.end()) {
                    boundaryEdges.push_back(edge);
                }
            });
        }

        if (boundaryEdges.empty()) return {};

        // Sort edges into a cycle
        std::vector<HalfEdge*> result;
        HalfEdge* current = nullptr;

        // Find start edge
        if (startEdge) {
            for (HalfEdge* e : boundaryEdges) {
                if (e == startEdge) {
                    current = startEdge;
                    break;
                }
            }
        }
        if (!current && !boundaryEdges.empty()) {
            current = boundaryEdges[0];
        }

        std::set<HalfEdge*> visited;
        std::set<HalfEdge*> boundarySet(boundaryEdges.begin(), boundaryEdges.end());

        while (current && visited.find(current) == visited.end()) {
            visited.insert(current);
            result.push_back(current);

            // Find next boundary edge
            HalfEdge* nextEdge = current->next;
            while (nextEdge && boundarySet.find(nextEdge) == boundarySet.end()) {
                if (nextEdge->twin) {
                    nextEdge = nextEdge->twin->next;
                } else {
                    break;
                }
            }
            current = (boundarySet.find(nextEdge) != boundarySet.end()) ? nextEdge : nullptr;
        }

        return result;
    }

    /**
     * Split faces into connected components
     */
    static std::vector<std::vector<Face*>> split(const std::vector<Face*>& faceList) {
        std::set<Face*> faceSet(faceList.begin(), faceList.end());
        std::set<Face*> visited;
        std::vector<std::vector<Face*>> components;

        for (Face* face : faceList) {
            if (visited.find(face) != visited.end()) continue;

            std::vector<Face*> component;
            std::vector<Face*> queue = {face};

            while (!queue.empty()) {
                Face* current = queue.back();
                queue.pop_back();

                if (visited.find(current) != visited.end()) continue;
                visited.insert(current);
                component.push_back(current);

                // Add adjacent faces in the set
                current->forEachEdge([&](HalfEdge* edge) {
                    if (edge->twin &&
                        faceSet.find(edge->twin->face) != faceSet.end() &&
                        visited.find(edge->twin->face) == visited.end()) {
                        queue.push_back(edge->twin->face);
                    }
                });
            }

            components.push_back(std::move(component));
        }

        return components;
    }

    /**
     * Split an edge at its midpoint
     */
    Vertex* splitEdge(HalfEdge* edge) {
        Point midpoint(
            (edge->origin->point.x + edge->destination()->point.x) / 2,
            (edge->origin->point.y + edge->destination()->point.y) / 2
        );

        auto newVertex = std::make_unique<Vertex>(midpoint);
        Vertex* newVertexPtr = newVertex.get();
        vertices[pointKey(midpoint)] = std::move(newVertex);

        auto newEdge = std::make_unique<HalfEdge>();
        HalfEdge* newEdgePtr = newEdge.get();

        newEdgePtr->origin = newVertexPtr;
        newEdgePtr->face = edge->face;
        newEdgePtr->next = edge->next;
        newEdgePtr->prev = edge;

        if (edge->next) edge->next->prev = newEdgePtr;
        edge->next = newEdgePtr;

        newVertexPtr->edges.push_back(newEdgePtr);
        edges.push_back(std::move(newEdge));

        if (edge->twin) {
            auto newTwin = std::make_unique<HalfEdge>();
            HalfEdge* newTwinPtr = newTwin.get();

            newTwinPtr->origin = newVertexPtr;
            newTwinPtr->face = edge->twin->face;
            newTwinPtr->twin = edge;
            edge->twin->twin = newEdgePtr;
            newEdgePtr->twin = edge->twin;
            edge->twin = newTwinPtr;

            newTwinPtr->next = edge->twin->next;
            newTwinPtr->prev = edge->twin;
            if (edge->twin->next) edge->twin->next->prev = newTwinPtr;
            edge->twin->next = newTwinPtr;

            newVertexPtr->edges.push_back(newTwinPtr);
            edges.push_back(std::move(newTwin));
        }

        return newVertexPtr;
    }

private:
    static std::string pointKey(const Point& p) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << p.x << "," << p.y;
        return oss.str();
    }
};

/**
 * Edge chain utilities
 */
class EdgeChain {
public:
    /**
     * Convert edge chain to polygon
     */
    static std::vector<Point> toPoly(const std::vector<HalfEdge*>& chain) {
        std::vector<Point> result;
        result.reserve(chain.size());
        for (HalfEdge* edge : chain) {
            result.push_back(edge->origin->point);
        }
        return result;
    }

    /**
     * Convert edge chain to polyline (includes last point)
     */
    static std::vector<Point> toPolyline(const std::vector<HalfEdge*>& chain) {
        if (chain.empty()) return {};

        std::vector<Point> points;
        points.reserve(chain.size() + 1);

        for (HalfEdge* edge : chain) {
            points.push_back(edge->origin->point);
        }

        if (chain.back()->destination()) {
            points.push_back(chain.back()->destination()->point);
        }

        return points;
    }

    /**
     * Get all vertices in edge chain
     */
    static std::vector<Vertex*> vertices(const std::vector<HalfEdge*>& chain) {
        std::vector<Vertex*> result;
        result.reserve(chain.size());
        for (HalfEdge* edge : chain) {
            result.push_back(edge->origin);
        }
        return result;
    }

    /**
     * Assign data to all edges in chain
     */
    static void assignData(const std::vector<HalfEdge*>& chain, void* data, bool overwrite = true) {
        for (HalfEdge* edge : chain) {
            if (overwrite || edge->data == nullptr) {
                edge->data = data;
            }
        }
    }

    /**
     * Find edge by origin vertex
     */
    static HalfEdge* edgeByOrigin(const std::vector<HalfEdge*>& chain, Vertex* vertex) {
        for (HalfEdge* edge : chain) {
            if (edge->origin == vertex) return edge;
        }
        return nullptr;
    }
};

} // namespace geom
} // namespace town_generator
