#include "DwellingPlan.h"
#include <algorithm>
#include <queue>
#include <set>
#include <map>

namespace dwelling {

std::string roomTypeName(RoomType type) {
    switch (type) {
        case RoomType::Hall: return "Hall";
        case RoomType::Kitchen: return "Kitchen";
        case RoomType::DiningRoom: return "Dining Room";
        case RoomType::LivingRoom: return "Living Room";
        case RoomType::Bedroom: return "Bedroom";
        case RoomType::Bathroom: return "Bathroom";
        case RoomType::Study: return "Study";
        case RoomType::Storage: return "Storage";
        case RoomType::Attic: return "Attic";
        case RoomType::Cellar: return "Cellar";
        case RoomType::Library: return "Library";
        case RoomType::Chapel: return "Chapel";
        case RoomType::Gallery: return "Gallery";
        case RoomType::Workshop: return "Workshop";
        case RoomType::Corridor: return "Corridor";
        case RoomType::Stairhall: return "Stairhall";
        case RoomType::Armoury: return "Armoury";
        case RoomType::Salon: return "Salon";
        case RoomType::Nursery: return "Nursery";
        case RoomType::Pantry: return "Pantry";
        default: return "Room";
    }
}

Room::Room(Plan* plan, const std::vector<Edge>& contour)
    : plan_(plan), contour_(contour)
{
    if (!contour.empty()) {
        area_ = plan_->grid()->contourToArea(contour);
    }
}

std::vector<Door*> Room::getDoors() const {
    std::vector<Door*> result;
    for (auto& door : const_cast<std::vector<Door>&>(plan_->doors())) {
        if (door.room1 == this || door.room2 == this) {
            result.push_back(&door);
        }
    }
    return result;
}

std::vector<Window*> Room::getWindows() const {
    std::vector<Window*> result;
    for (auto& window : const_cast<std::vector<Window>&>(plan_->windows())) {
        if (window.room == this) {
            result.push_back(&window);
        }
    }
    return result;
}

bool Room::contains(const Cell& c) const {
    for (const Cell* cell : area_) {
        if (*cell == c) return true;
    }
    return false;
}

bool Room::hasEdge(const Edge& e) const {
    for (const Edge& ce : contour_) {
        if ((ce.a == e.a && ce.b == e.b) || (ce.a == e.b && ce.b == e.a)) {
            return true;
        }
    }
    return false;
}

Plan::Plan(Grid* grid, const std::vector<Cell*>& area, uint32_t seed)
    : grid_(grid), area_(area), rng_(seed)
{
    contour_ = grid_->outline(area);
}

Room* Plan::getRoom(const Cell& c) {
    for (Room* room : rooms_) {
        if (room->contains(c)) {
            return room;
        }
    }
    return nullptr;
}

Room* Plan::getRoom(const Edge& e) {
    Cell c = e.adjacentCell();
    return getRoom(c);
}

void Plan::generate() {
    innerWalls_.clear();
    rooms_.clear();
    ownedRooms_.clear();
    doors_.clear();
    windows_.clear();
    entrance_ = nullptr;

    // Divide the area into rooms
    divideArea(contour_);

    // Merge narrow corridor-like rooms
    mergeCorridors();

    // Connect rooms with doors
    connectRooms();

    // Place entrance
    if (!rooms_.empty() && !contour_.empty()) {
        // Find a good exterior edge for entrance
        std::uniform_int_distribution<size_t> dist(0, contour_.size() - 1);
        size_t idx = dist(rng_);

        Room* entranceRoom = getRoom(contour_[idx]);
        if (entranceRoom) {
            Door entrance;
            entrance.room1 = nullptr;
            entrance.room2 = entranceRoom;
            entrance.edge = contour_[idx];
            entrance.type = DoorType::Regular;
            doors_.push_back(entrance);
            entrance_ = &doors_.back();
        }
    }
}

bool Plan::isNarrow(const std::vector<Cell*>& area, const Cell& c) const {
    bool hasNorth = false, hasSouth = false, hasEast = false, hasWest = false;

    for (const Cell* cell : area) {
        if (cell->i == c.i - 1 && cell->j == c.j) hasNorth = true;
        if (cell->i == c.i + 1 && cell->j == c.j) hasSouth = true;
        if (cell->i == c.i && cell->j == c.j + 1) hasEast = true;
        if (cell->i == c.i && cell->j == c.j - 1) hasWest = true;
    }

    // Narrow if missing neighbors on opposite sides
    if (!hasNorth && !hasSouth) return true;
    if (!hasEast && !hasWest) return true;

    return false;
}

Edge* Plan::getNotch(const std::vector<Edge>& contour) {
    std::vector<Cell*> area = grid_->contourToArea(contour);
    std::vector<Edge*> candidates;

    for (size_t i = 0; i < contour.size(); ++i) {
        const Edge& current = contour[i];
        const Edge& prev = contour[(i + contour.size() - 1) % contour.size()];

        if (current.dir == prev.dir) {
            // Straight edge - good for wall placement
            Cell* c1 = grid_->edgeToCell(prev);
            Cell* c2 = grid_->edgeToCell(current);
            bool narrow1 = c1 && isNarrow(area, *c1);
            bool narrow2 = c2 && isNarrow(area, *c2);
            if (!narrow1 || !narrow2) {
                candidates.push_back(const_cast<Edge*>(&contour[i]));
            }
        } else if (current.dir == counterClockwise(prev.dir)) {
            // Convex corner - good for notch
            candidates.push_back(const_cast<Edge*>(&contour[i]));
        }
    }

    if (candidates.empty()) return nullptr;

    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng_)];
}

std::vector<Edge> Plan::findWall(const std::vector<Edge>& contour, const Edge& start, Dir direction) {
    std::vector<Edge> wall;
    wall.push_back(start);

    // Extend wall in the given direction
    Node current = start.b;
    while (true) {
        // Find next edge
        bool found = false;
        for (const Edge& e : contour) {
            if (e.a == current && e.dir == direction) {
                wall.push_back(e);
                current = e.b;
                found = true;
                break;
            }
        }
        if (!found) break;
    }

    return wall;
}

void Plan::divideArea(const std::vector<Edge>& contour) {
    if (contour.empty()) return;

    std::vector<Cell*> area = grid_->contourToArea(contour);

    if (area.empty()) return;

    // Check if area is small enough for a room
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float sizeVariation = (dist(rng_) + dist(rng_) + dist(rng_)) / 3.0f;

    // Style affects room size variation
    float styleMod = 1.0f;
    switch (params_.style) {
        case DwellingStyle::Mechanical:
            // More regular, less variation
            styleMod = 0.8f;
            sizeVariation = sizeVariation * 0.5f + 0.25f;  // Reduce variation
            break;
        case DwellingStyle::Organic:
            // More irregular, more variation
            styleMod = 1.2f;
            break;
        case DwellingStyle::Gothic:
            // Larger rooms
            styleMod = 1.4f;
            break;
        default:
            break;
    }

    float threshold = params_.avgRoomSize * styleMod * (0.5f + sizeVariation);

    if (static_cast<float>(area.size()) <= threshold || area.size() <= 3) {
        addRoom(contour);
        return;
    }

    // Find bounds
    int minI = area[0]->i, maxI = area[0]->i;
    int minJ = area[0]->j, maxJ = area[0]->j;
    for (const Cell* c : area) {
        minI = std::min(minI, c->i);
        maxI = std::max(maxI, c->i);
        minJ = std::min(minJ, c->j);
        maxJ = std::max(maxJ, c->j);
    }

    int rangeI = maxI - minI + 1;
    int rangeJ = maxJ - minJ + 1;

    std::vector<Cell*> side1, side2;

    // Style affects split behavior
    bool preferVertical = (rangeJ >= rangeI);
    bool preferHorizontal = (rangeI >= rangeJ);

    // Mechanical style prefers splitting at corners (more rectangular rooms)
    if (params_.style == DwellingStyle::Mechanical || params_.preferCorners) {
        // Split at exact halves for regularity
        if (preferHorizontal) {
            int midI = minI + rangeI / 2;
            for (Cell* c : area) {
                if (c->i < midI) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        } else {
            int midJ = minJ + rangeJ / 2;
            for (Cell* c : area) {
                if (c->j < midJ) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        }
    }
    // Organic style adds randomness to split positions
    else if (params_.style == DwellingStyle::Organic || params_.preferWalls) {
        std::uniform_real_distribution<float> splitDist(0.3f, 0.7f);
        float splitPos = splitDist(rng_);

        if (preferHorizontal) {
            int midI = minI + static_cast<int>(rangeI * splitPos);
            for (Cell* c : area) {
                if (c->i < midI) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        } else {
            int midJ = minJ + static_cast<int>(rangeJ * splitPos);
            for (Cell* c : area) {
                if (c->j < midJ) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        }
    }
    // Default/Natural style
    else {
        // Split along the longer dimension with slight randomness
        std::uniform_int_distribution<int> offsetDist(-1, 1);
        int offset = offsetDist(rng_);

        if (rangeI >= rangeJ) {
            int midI = minI + rangeI / 2 + offset;
            for (Cell* c : area) {
                if (c->i < midI) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        } else {
            int midJ = minJ + rangeJ / 2 + offset;
            for (Cell* c : area) {
                if (c->j < midJ) {
                    side1.push_back(c);
                } else {
                    side2.push_back(c);
                }
            }
        }
    }

    // Handle edge cases
    if (side1.empty() || side2.empty()) {
        addRoom(contour);
        return;
    }

    // Recursively divide each side if connected
    if (grid_->isConnected(side1)) {
        std::vector<Edge> contour1 = grid_->outline(side1);
        if (!contour1.empty()) {
            divideArea(contour1);
        }
    } else {
        // Add disconnected parts as separate rooms
        addRoom(grid_->outline(side1));
    }

    if (grid_->isConnected(side2)) {
        std::vector<Edge> contour2 = grid_->outline(side2);
        if (!contour2.empty()) {
            divideArea(contour2);
        }
    } else {
        addRoom(grid_->outline(side2));
    }
}

Room* Plan::addRoom(const std::vector<Edge>& contour) {
    if (contour.empty()) return nullptr;

    auto room = std::make_unique<Room>(this, contour);
    if (room->area().empty()) return nullptr;  // Invalid room

    Room* ptr = room.get();
    rooms_.push_back(ptr);
    ownedRooms_.push_back(std::move(room));
    return ptr;
}

void Plan::mergeCorridors() {
    // Find corridor-like rooms (all cells are narrow)
    std::vector<Room*> corridors;
    for (Room* room : rooms_) {
        bool allNarrow = true;
        for (const Cell* c : room->area()) {
            if (!isNarrow(room->area(), *c)) {
                allNarrow = false;
                break;
            }
        }
        if (allNarrow) {
            corridors.push_back(room);
        }
    }

    // Try to merge corridors with adjacent rooms
    // (Simplified - just keep them as separate rooms for now)
}

void Plan::connectRooms() {
    if (rooms_.size() <= 1) return;

    // Find shared edges between rooms and place doors
    std::set<std::pair<Room*, Room*>> connected;

    for (size_t i = 0; i < rooms_.size(); ++i) {
        for (size_t j = i + 1; j < rooms_.size(); ++j) {
            Room* r1 = rooms_[i];
            Room* r2 = rooms_[j];

            // Find shared edges
            std::vector<Edge> sharedEdges;
            for (const Edge& e1 : r1->contour()) {
                for (const Edge& e2 : r2->contour()) {
                    if ((e1.a == e2.b && e1.b == e2.a)) {
                        sharedEdges.push_back(e1);
                    }
                }
            }

            if (!sharedEdges.empty() && connected.find({r1, r2}) == connected.end()) {
                // Place a door at a random shared edge
                std::uniform_int_distribution<size_t> dist(0, sharedEdges.size() - 1);
                Edge doorEdge = sharedEdges[dist(rng_)];

                Door door;
                door.room1 = r1;
                door.room2 = r2;
                door.edge = doorEdge;
                door.type = DoorType::Regular;
                doors_.push_back(door);

                connected.insert({r1, r2});
                connected.insert({r2, r1});
            }
        }
    }
}

void Plan::assignRooms() {
    if (rooms_.empty()) return;

    // Sort rooms by size (largest first)
    std::vector<Room*> sortedRooms = rooms_;
    std::sort(sortedRooms.begin(), sortedRooms.end(),
        [](Room* a, Room* b) { return a->size() > b->size(); });

    // Find entrance room
    Room* entranceRoom = nullptr;
    if (entrance_) {
        entranceRoom = getRoom(entrance_->edge);
    }

    // Identify narrow/corridor rooms first
    for (Room* room : rooms_) {
        bool allNarrow = true;
        for (const Cell* c : room->area()) {
            if (!isNarrow(room->area(), *c)) {
                allNarrow = false;
                break;
            }
        }
        if (allNarrow && room->size() >= 2) {
            room->setType(RoomType::Corridor);
        }
    }

    // Room type pools based on style
    std::vector<RoomType> primaryTypes;
    std::vector<RoomType> secondaryTypes;
    std::vector<RoomType> smallTypes;

    if (params_.style == DwellingStyle::Gothic) {
        // Gothic/castle style room types
        primaryTypes = {
            RoomType::Gallery,
            RoomType::Chapel,
            RoomType::Library,
            RoomType::Salon,
        };
        secondaryTypes = {
            RoomType::Bedroom,
            RoomType::Study,
            RoomType::Armoury,
            RoomType::Workshop,
        };
        smallTypes = {
            RoomType::Storage,
            RoomType::Pantry,
            RoomType::Bathroom,
        };
    } else {
        // Standard home style room types
        primaryTypes = {
            RoomType::LivingRoom,
            RoomType::Kitchen,
            RoomType::DiningRoom,
        };
        secondaryTypes = {
            RoomType::Bedroom,
            RoomType::Study,
            RoomType::Library,
        };
        smallTypes = {
            RoomType::Bathroom,
            RoomType::Pantry,
            RoomType::Storage,
        };
    }

    // Add nursery for organic/natural style
    if (params_.style == DwellingStyle::Organic || params_.style == DwellingStyle::Natural) {
        secondaryTypes.push_back(RoomType::Nursery);
    }

    int primaryIdx = 0, secondaryIdx = 0, smallIdx = 0;
    int bedroomCount = 0;

    for (Room* room : sortedRooms) {
        // Skip already assigned rooms
        if (room->type() != RoomType::Unassigned) continue;

        if (room == entranceRoom) {
            room->setType(RoomType::Hall);
        } else if (room->size() >= 6 && primaryIdx < static_cast<int>(primaryTypes.size())) {
            // Large rooms get primary types
            room->setType(primaryTypes[primaryIdx++]);
        } else if (room->size() >= 4 && secondaryIdx < static_cast<int>(secondaryTypes.size())) {
            // Medium rooms get secondary types
            room->setType(secondaryTypes[secondaryIdx++]);
        } else if (room->size() <= 3 && smallIdx < static_cast<int>(smallTypes.size())) {
            // Small rooms get utility types
            room->setType(smallTypes[smallIdx++]);
        } else if (room->size() >= 4) {
            // Additional bedrooms
            room->setType(RoomType::Bedroom);
            bedroomCount++;
        } else {
            // Very small rooms become storage
            room->setType(RoomType::Storage);
        }
    }
}

void Plan::assignDoors() {
    // Doors are already assigned in connectRooms()
    // This could be enhanced to set door types based on room types
    for (Door& door : doors_) {
        if (door.isExterior()) {
            door.type = DoorType::Regular;
        } else {
            // Bathrooms get regular doors, other rooms may have doorways
            if ((door.room1 && door.room1->type() == RoomType::Bathroom) ||
                (door.room2 && door.room2->type() == RoomType::Bathroom)) {
                door.type = DoorType::Regular;
            } else {
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                door.type = dist(rng_) < 0.7f ? DoorType::Regular : DoorType::Doorway;
            }
        }
    }
}

void Plan::spawnWindows() {
    windows_.clear();

    for (Room* room : rooms_) {
        // Find exterior edges
        std::vector<Edge> exteriorEdges;
        for (const Edge& e : room->contour()) {
            bool isExterior = false;
            for (const Edge& ce : contour_) {
                if (e.a == ce.a && e.b == ce.b) {
                    isExterior = true;
                    break;
                }
            }

            // Check it's not a door
            bool isDoor = false;
            for (const Door& d : doors_) {
                if ((d.edge.a == e.a && d.edge.b == e.b) ||
                    (d.edge.a == e.b && d.edge.b == e.a)) {
                    isDoor = true;
                    break;
                }
            }

            if (isExterior && !isDoor) {
                exteriorEdges.push_back(e);
            }
        }

        // Place windows on some exterior edges
        int numWindows = static_cast<int>(exteriorEdges.size() * params_.windowDensity);
        std::shuffle(exteriorEdges.begin(), exteriorEdges.end(), rng_);

        for (int i = 0; i < numWindows && i < static_cast<int>(exteriorEdges.size()); ++i) {
            Window window;
            window.room = room;
            window.edge = exteriorEdges[i];
            windows_.push_back(window);
        }
    }
}

void Plan::spawnStairs(bool hasFloorAbove, bool hasFloorBelow) {
    // Clear existing stairs
    stairs_.clear();

    // Find suitable rooms for stairs (prefer larger rooms, not bathrooms)
    std::vector<Room*> candidates;
    for (Room* room : rooms_) {
        if (room->type() == RoomType::Bathroom ||
            room->type() == RoomType::Pantry ||
            room->type() == RoomType::Storage) {
            continue;  // Skip these room types
        }
        if (room->size() >= 2) {  // Need at least 2 cells for stairs
            candidates.push_back(room);
        }
    }

    if (candidates.empty()) return;

    // Sort by size (prefer medium-sized rooms)
    std::sort(candidates.begin(), candidates.end(), [](Room* a, Room* b) {
        // Prefer rooms that are medium-sized (not too big, not too small)
        int scoreA = std::abs(static_cast<int>(a->size()) - 4);
        int scoreB = std::abs(static_cast<int>(b->size()) - 4);
        return scoreA < scoreB;
    });

    // Find a stair hall or corridor if one exists
    Room* stairRoom = nullptr;
    for (Room* room : candidates) {
        if (room->type() == RoomType::Stairhall ||
            room->type() == RoomType::Corridor ||
            room->type() == RoomType::Hall) {
            stairRoom = room;
            break;
        }
    }

    // Fall back to first candidate
    if (!stairRoom && !candidates.empty()) {
        stairRoom = candidates[0];
    }

    if (!stairRoom || stairRoom->area().empty()) return;

    // Pick a cell in the room for the stair
    std::uniform_int_distribution<size_t> cellDist(0, stairRoom->area().size() - 1);
    const Cell* stairCell = stairRoom->area()[cellDist(rng_)];

    // Determine direction (prefer not facing a wall)
    std::vector<Dir> possibleDirs = {Dir::North, Dir::East, Dir::South, Dir::West};
    std::shuffle(possibleDirs.begin(), possibleDirs.end(), rng_);
    Dir stairDir = possibleDirs[0];

    // Determine stair type (mostly regular, occasionally spiral)
    std::uniform_real_distribution<float> typeDist(0.0f, 1.0f);
    StairType type = typeDist(rng_) < 0.2f ? StairType::Spiral : StairType::Regular;

    // Place stairs
    if (hasFloorAbove) {
        Stair upStair;
        upStair.cell = *stairCell;
        upStair.direction = stairDir;
        upStair.goingUp = true;
        upStair.type = type;
        upStair.room = stairRoom;
        stairs_.push_back(upStair);
    }

    if (hasFloorBelow) {
        // Use the same location but mark as going down
        Stair downStair;
        downStair.cell = *stairCell;
        downStair.direction = opposite(stairDir);  // Opposite direction
        downStair.goingUp = false;
        downStair.type = type;
        downStair.room = stairRoom;
        stairs_.push_back(downStair);
    }

    // Mark the room as a stairhall
    if (stairRoom && (hasFloorAbove || hasFloorBelow)) {
        stairRoom->setType(RoomType::Stairhall);
    }
}

void Plan::setStairPosition(const Cell& cell, Dir direction, StairType type, bool goingUp) {
    // Add a stair at a specific position (for alignment with other floors)
    Room* room = getRoom(cell);
    if (!room) return;

    Stair stair;
    stair.cell = cell;
    stair.direction = direction;
    stair.goingUp = goingUp;
    stair.type = type;
    stair.room = room;
    stairs_.push_back(stair);
}

} // namespace dwelling
