#pragma once

#include <string>
#include <cstdint>

namespace economy {

/**
 * Category of menu items for grouping and display
 */
enum class MenuCategory {
    Food,
    Drink,
    Dessert,
    Special
};

/**
 * Represents a single item on a menu (food, drink, etc.)
 *
 * Prices are stored in copper coins (smallest currency unit).
 * For display: 100 copper = 1 silver, 100 silver = 1 gold
 */
struct MenuItem {
    std::string id;           // Unique identifier (e.g., "ale_common")
    std::string name;         // Display name (e.g., "Common Ale")
    std::string description;  // Optional description
    MenuCategory category = MenuCategory::Food;
    uint32_t basePrice = 0;   // Base price in copper coins
    bool available = true;    // Whether item is currently available

    // Builder-style setters for convenient construction
    MenuItem& setId(const std::string& val) { id = val; return *this; }
    MenuItem& setName(const std::string& val) { name = val; return *this; }
    MenuItem& setDescription(const std::string& val) { description = val; return *this; }
    MenuItem& setCategory(MenuCategory val) { category = val; return *this; }
    MenuItem& setBasePrice(uint32_t val) { basePrice = val; return *this; }
    MenuItem& setAvailable(bool val) { available = val; return *this; }
};

/**
 * Helper to format price in copper to a readable string
 * e.g., 1234 copper -> "12s 34c" or "1g 2s 34c"
 */
inline std::string formatPrice(uint32_t copperCoins) {
    uint32_t gold = copperCoins / 10000;
    uint32_t silver = (copperCoins % 10000) / 100;
    uint32_t copper = copperCoins % 100;

    std::string result;
    if (gold > 0) {
        result += std::to_string(gold) + "g ";
    }
    if (silver > 0 || gold > 0) {
        result += std::to_string(silver) + "s ";
    }
    result += std::to_string(copper) + "c";
    return result;
}

} // namespace economy
