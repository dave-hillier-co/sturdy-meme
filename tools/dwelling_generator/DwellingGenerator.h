// Dwelling generator - procedural building floor plan generation
// Ported from watabou's Dwellings (https://watabou.itch.io/dwellings)

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <cstdint>
#include <functional>
#include <random>
#include <map>
#include <set>

namespace dwelling {

// Forward declarations
struct Cell;
struct Node;
struct Edge;
struct Room;
struct Door;
struct Floor;
struct Dwelling;

// Direction enumeration
enum class Dir {
    North,
    East,
    South,
    West
};

Dir clockwise(Dir d);
Dir counterClockwise(Dir d);
Dir opposite(Dir d);
int deltaI(Dir d);
int deltaJ(Dir d);

// Room types
enum class RoomType {
    Generic,
    Corridor,
    Hall,
    Kitchen,
    DiningRoom,
    LivingRoom,
    Bedroom,
    GuestRoom,
    Bathroom,
    Study,
    Library,
    Office,
    Storage,
    Cellar,
    Attic,
    Stairwell,
    SecretPassage,
    Armory,
    Greenhouse,
    Observatory,
    Laboratory,
    Gallery,
    Chapel,
    Servant,
    Nursery,
    Pantry,
    Lookout
};

std::string roomTypeName(RoomType type);

// Door types
enum class DoorType {
    None,
    Doorway,
    Regular,
    Secret
};

// 2D integer point
struct Point {
    int i = 0;
    int j = 0;

    bool operator==(const Point& other) const {
        return i == other.i && j == other.j;
    }
    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
    bool operator<(const Point& other) const {
        if (i != other.i) return i < other.i;
        return j < other.j;
    }
};

// Node in the grid (at cell corners)
struct Node {
    int i = 0;
    int j = 0;
    int id = 0;

    Point point() const { return {i, j}; }

    bool operator==(const Node& other) const {
        return i == other.i && j == other.j;
    }
};

// Edge between two nodes
struct Edge {
    Node* a = nullptr;
    Node* b = nullptr;
    Dir dir = Dir::North;

    Point point() const {
        return {(a->i + b->i) / 2, (a->j + b->j) / 2};
    }

    bool operator==(const Edge& other) const {
        return a == other.a && b == other.b;
    }
};

// Cell in the grid
struct Cell {
    int i = 0;
    int j = 0;
    Room* room = nullptr;

    Point point() const { return {i, j}; }

    bool operator==(const Cell& other) const {
        return i == other.i && j == other.j;
    }
};

// Grid of cells with nodes and edges
class Grid {
public:
    Grid(int width, int height);

    int width() const { return w; }
    int height() const { return h; }

    Node* node(int i, int j);
    Cell* cell(int i, int j);
    Edge* nodeToEdge(Node* n, Dir dir);
    Edge* cellToEdge(Cell* c, Dir dir);
    Edge* edgeBetween(Node* a, Node* b);
    Cell* edgeToCell(Edge* e);

    std::vector<Edge*> outline(const std::vector<Cell*>& area);
    std::vector<Cell*> contourToArea(const std::vector<Edge*>& contour);
    bool isConnected(const std::vector<Cell*>& area);
    std::vector<Edge*> revertChain(const std::vector<Edge*>& chain);

private:
    int w, h;
    std::vector<std::vector<std::unique_ptr<Node>>> nodes;
    std::vector<std::vector<std::unique_ptr<Cell>>> cells;
    std::map<std::pair<int, int>, std::unique_ptr<Edge>> edges;

    void createEdges();
    std::pair<int, int> edgeKey(Node* a, Node* b);
};

// Door connecting two rooms
struct Door {
    Room* room1 = nullptr;
    Room* room2 = nullptr;
    Edge* edge1 = nullptr;
    Edge* edge2 = nullptr;
    DoorType type = DoorType::Regular;

    int getPrice() const;
};

// Window on an edge
struct Window {
    Edge* edge = nullptr;
};

// Staircase between floors
struct Staircase {
    Cell* cell = nullptr;
    Dir dir = Dir::North;
    Floor* from = nullptr;
    Floor* to = nullptr;
    bool spiral = false;
};

// Room in a floor plan
struct Room {
    Floor* floor = nullptr;
    std::vector<Cell*> area;
    std::vector<Cell*> narrow;
    std::vector<Edge*> contour;
    RoomType type = RoomType::Generic;
    std::map<Room*, Door*> doors;

    int size() const { return static_cast<int>(area.size()); }
    int countDoors() const;
    std::vector<Door*> getDoors();
    bool hasExit() const;
    bool hasSpiral() const;
    void link(Room* other, Edge* edge, Door* door);
    void unlink(Room* other);
};

// Stairwell configuration
struct Stairwell {
    Cell* stair = nullptr;
    Cell* landing = nullptr;
    Dir exit = Dir::North;
    Room* room = nullptr;
};

// Spiral staircase configuration
struct Spiral {
    Edge* entrance = nullptr;
    Edge* exit = nullptr;
    Cell* landing = nullptr;
};

// Entrance configuration
struct Entrance {
    Edge* door = nullptr;
    Cell* landing = nullptr;
};

// Floor plan for one level
struct Floor {
    Dwelling* dwelling = nullptr;
    std::unique_ptr<Grid> grid;
    std::vector<std::unique_ptr<Room>> rooms;
    std::vector<std::unique_ptr<Door>> doorList;
    std::vector<Edge*> contour;
    std::vector<Cell*> area;
    std::vector<Window> windows;
    std::vector<Staircase> stairs;
    std::optional<Entrance> entrance;
    std::optional<Stairwell> stairwell;
    std::optional<Spiral> spiral;
    std::vector<Edge*> innerWalls;

    int getFloorIndex() const;
    bool isGroundFloor() const;
    bool isTopFloor() const;
    Room* getRoom(Cell* cell);
    Room* edgeToRoom(Edge* e);
    Room* addRoom(const std::vector<Edge*>& contour);
    Room* findStart();
    std::vector<Door*> getDoors();

    void divideArea(const std::vector<Edge*>& contour);
    void connectRooms();
    void mergeCorridors();
    void assignRooms();
    void spawnWindows();

private:
    static constexpr float avgRoomSize = 6.0f;
    static constexpr float roomSizeChaos = 1.0f;
    static constexpr float connectivity = 0.5f;
    static constexpr float windowDensity = 0.7f;
    bool preferCorners = false;
    bool preferWalls = false;
    bool noNooks = true;
    bool regularRooms = false;

    bool isNarrow(const std::vector<Cell*>& area, Cell* cell);
    Edge* getNotch(const std::vector<Edge*>& contour);
    std::vector<Door*> wallDoors();
};

// Complete dwelling with multiple floors
struct Dwelling {
    std::string name;
    std::vector<std::unique_ptr<Floor>> floors;
    std::unique_ptr<Floor> basement;
    int seed = 0;

    int floorCount() const { return static_cast<int>(floors.size()); }
};

// Blueprint/configuration for dwelling generation
struct Blueprint {
    int seed = 0;
    int numFloors = 0;  // 0 = random
    std::string size = "medium";  // small, medium, large
    bool square = false;
    bool hasBasement = false;
    std::vector<std::string> tags;

    bool hasTag(const std::string& tag) const;
};

// Generator configuration
struct DwellingConfig {
    std::string outputDir = ".";
    int seed = 0;
    int count = 1;
    std::string size = "medium";
    int numFloors = 0;
    bool square = false;
    bool basement = false;
    bool spiral = false;
    bool stairwell = false;
    std::vector<std::string> tags;
};

// Main generator class
class DwellingGenerator {
public:
    DwellingGenerator();

    bool generate(const DwellingConfig& config,
                  std::function<void(float, const std::string&)> progress = nullptr);

    bool saveDwellings(const std::string& path);
    bool saveDwellingsSVG(const std::string& path);
    bool saveDwellingsGeoJSON(const std::string& path);

    const std::vector<std::unique_ptr<Dwelling>>& getDwellings() const { return dwellings; }

private:
    std::vector<std::unique_ptr<Dwelling>> dwellings;
    std::mt19937 rng;

    std::unique_ptr<Dwelling> generateDwelling(const Blueprint& bp);

    // Shape generation
    struct ShapeResult {
        std::unique_ptr<Grid> grid;
        std::vector<Cell*> area;
    };

    ShapeResult getShape(int minSize, int maxSize, bool isSquare);
    std::vector<Point> getPolyomino(int minSize, int maxSize);
    std::vector<Point> getBox(int minSize, int maxSize);

    // Random helpers
    float random();
    int randomInt(int min, int max);
    template<typename T>
    T& randomChoice(std::vector<T>& vec);
    template<typename T>
    const T& randomChoice(const std::vector<T>& vec);
    template<typename T>
    T& weightedChoice(std::vector<T>& items, const std::vector<float>& weights);
};

// SVG output helper
class DwellingSVG {
public:
    static std::string generate(const Dwelling& dwelling, int floor, float scale = 50.0f);
    static std::string generateMultiFloor(const Dwelling& dwelling, float scale = 50.0f);

private:
    static std::string roomColor(RoomType type);
    static void drawRoom(std::string& svg, const Room& room, float scale, float offsetX, float offsetY);
    static void drawDoor(std::string& svg, const Door& door, float scale, float offsetX, float offsetY);
    static void drawWalls(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY);
    static void drawWindows(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY);
    static void drawStairs(std::string& svg, const Floor& floor, float scale, float offsetX, float offsetY);
};

}  // namespace dwelling
