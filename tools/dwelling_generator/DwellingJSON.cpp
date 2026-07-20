#include "DwellingJSON.h"
#include "DwellingPlan.h"
#include <SDL3/SDL_log.h>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace dwelling {

namespace {

json edgeToJson(const Edge& e) {
    return json{{"a", {e.a.j, e.a.i}}, {"b", {e.b.j, e.b.i}}};
}

json planToJson(const Plan& plan) {
    json floor;

    json rooms = json::array();
    for (const Room* room : plan.rooms()) {
        json r;
        r["type"] = roomTypeName(room->type());
        r["name"] = room->name();
        json cells = json::array();
        for (const Cell* c : room->area()) {
            cells.push_back({c->j, c->i});
        }
        r["cells"] = cells;
        json contour = json::array();
        for (const Edge& e : room->contour()) {
            contour.push_back(edgeToJson(e));
        }
        r["contour"] = contour;
        rooms.push_back(std::move(r));
    }
    floor["rooms"] = rooms;

    json doors = json::array();
    for (const Door& door : plan.doors()) {
        json d;
        d["edge"] = edgeToJson(door.edge);
        d["exterior"] = door.isExterior();
        switch (door.type) {
            case DoorType::Doorway: d["type"] = "doorway"; break;
            case DoorType::Double:  d["type"] = "double"; break;
            default:                d["type"] = "regular"; break;
        }
        d["entrance"] = (plan.entrance() == &door);
        doors.push_back(std::move(d));
    }
    floor["doors"] = doors;

    json windows = json::array();
    for (const Window& window : plan.windows()) {
        windows.push_back(json{{"edge", edgeToJson(window.edge)}});
    }
    floor["windows"] = windows;

    json stairs = json::array();
    for (const Stair& stair : plan.stairs()) {
        json s;
        s["cell"] = {stair.cell.j, stair.cell.i};
        s["up"] = stair.goingUp;
        s["type"] = stair.type == StairType::Spiral ? "spiral" : "regular";
        stairs.push_back(std::move(s));
    }
    floor["stairs"] = stairs;

    return floor;
}

} // namespace

bool writeDwellingJson(const std::string& path, const House& house) {
    json root;
    root["name"] = house.name();
    root["grid"] = {{"width", house.gridWidth()}, {"height", house.gridHeight()}};

    json floors = json::array();
    for (int f = 0; f < house.numFloors(); ++f) {
        const Plan* plan = house.floor(f);
        if (!plan) continue;
        json floor = planToJson(*plan);
        floor["level"] = f;
        floors.push_back(std::move(floor));
    }
    if (const Plan* basement = house.basement()) {
        json floor = planToJson(*basement);
        floor["level"] = -1;
        floors.push_back(std::move(floor));
    }
    root["floors"] = floors;

    std::ofstream file(path);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to create dwelling JSON: %s", path.c_str());
        return false;
    }
    file << root.dump(2);
    if (!file.good()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Error writing dwelling JSON: %s", path.c_str());
        return false;
    }

    SDL_Log("Saved dwelling JSON: %s (%zu floors)", path.c_str(), floors.size());
    return true;
}

} // namespace dwelling
