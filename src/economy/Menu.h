#pragma once

#include "MenuItem.h"
#include "Discount.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <optional>

namespace economy {

/**
 * Result of calculating the final price for a menu item
 */
struct PricedItem {
    const MenuItem* item = nullptr;
    uint32_t basePrice = 0;
    uint32_t finalPrice = 0;
    uint32_t totalDiscount = 0;
    std::vector<std::string> appliedDiscountNames;  // Names of discounts that were applied

    bool hasDiscount() const { return totalDiscount > 0; }
    float discountPercentage() const {
        if (basePrice == 0) return 0.0f;
        return (static_cast<float>(totalDiscount) / static_cast<float>(basePrice)) * 100.0f;
    }
};

/**
 * A menu containing items and associated discounts
 *
 * Menus can belong to establishments (taverns, inns, etc.) and support
 * multiple concurrent discount types.
 */
class Menu {
public:
    Menu() = default;
    explicit Menu(const std::string& name) : menuName(name) {}

    // Menu identity
    void setName(const std::string& name) { menuName = name; }
    const std::string& getName() const { return menuName; }

    void setEstablishment(const std::string& name) { establishmentName = name; }
    const std::string& getEstablishment() const { return establishmentName; }

    // Item management
    void addItem(const MenuItem& item) {
        items.push_back(item);
        itemIndex[item.id] = items.size() - 1;
    }

    void removeItem(const std::string& itemId) {
        auto it = itemIndex.find(itemId);
        if (it != itemIndex.end()) {
            items.erase(items.begin() + it->second);
            rebuildIndex();
        }
    }

    const MenuItem* getItem(const std::string& itemId) const {
        auto it = itemIndex.find(itemId);
        if (it != itemIndex.end()) {
            return &items[it->second];
        }
        return nullptr;
    }

    MenuItem* getItemMutable(const std::string& itemId) {
        auto it = itemIndex.find(itemId);
        if (it != itemIndex.end()) {
            return &items[it->second];
        }
        return nullptr;
    }

    const std::vector<MenuItem>& getItems() const { return items; }

    std::vector<MenuItem> getItemsByCategory(MenuCategory category) const {
        std::vector<MenuItem> result;
        for (const auto& item : items) {
            if (item.category == category) {
                result.push_back(item);
            }
        }
        return result;
    }

    // Discount management
    void addHappyHourDiscount(const HappyHourDiscount& discount) {
        happyHourDiscounts.push_back(discount);
    }

    void addDailyDiscount(const DailyDiscount& discount) {
        dailyDiscounts.push_back(discount);
    }

    void addWeeklyDiscount(const WeeklyDiscount& discount) {
        weeklyDiscounts.push_back(discount);
    }

    void clearAllDiscounts() {
        happyHourDiscounts.clear();
        dailyDiscounts.clear();
        weeklyDiscounts.clear();
    }

    const std::vector<HappyHourDiscount>& getHappyHourDiscounts() const { return happyHourDiscounts; }
    const std::vector<DailyDiscount>& getDailyDiscounts() const { return dailyDiscounts; }
    const std::vector<WeeklyDiscount>& getWeeklyDiscounts() const { return weeklyDiscounts; }

    /**
     * Calculate the final price for an item given current time conditions
     *
     * @param itemId The item to price
     * @param timeOfDay Normalized time (0-1) from TimeSystem
     * @param dayOfWeek Current day of week
     * @param weekOfMonth Current week of month (1-5)
     * @param stackDiscounts If true, all applicable discounts stack. If false, use best discount.
     */
    PricedItem calculatePrice(
        const std::string& itemId,
        float timeOfDay,
        DayOfWeek dayOfWeek,
        int weekOfMonth,
        bool stackDiscounts = false
    ) const {
        const MenuItem* item = getItem(itemId);
        if (!item) {
            return PricedItem{};
        }
        return calculatePrice(*item, timeOfDay, dayOfWeek, weekOfMonth, stackDiscounts);
    }

    PricedItem calculatePrice(
        const MenuItem& item,
        float timeOfDay,
        DayOfWeek dayOfWeek,
        int weekOfMonth,
        bool stackDiscounts = false
    ) const {
        PricedItem result;
        result.item = &item;
        result.basePrice = item.basePrice;

        if (!item.available) {
            result.finalPrice = 0;
            return result;
        }

        // Collect all active discounts that apply to this item
        std::vector<std::pair<const DiscountInfo*, std::string>> applicableDiscounts;

        // Check happy hour discounts
        for (const auto& discount : happyHourDiscounts) {
            if (discount.isActive(timeOfDay, dayOfWeek) && discount.info.appliesTo(item)) {
                applicableDiscounts.push_back({&discount.info, discount.info.name});
            }
        }

        // Check daily discounts
        for (const auto& discount : dailyDiscounts) {
            if (discount.isActive(dayOfWeek) && discount.info.appliesTo(item)) {
                applicableDiscounts.push_back({&discount.info, discount.info.name});
            }
        }

        // Check weekly discounts
        for (const auto& discount : weeklyDiscounts) {
            if (discount.isActive(weekOfMonth) && discount.info.appliesTo(item)) {
                applicableDiscounts.push_back({&discount.info, discount.info.name});
            }
        }

        if (applicableDiscounts.empty()) {
            result.finalPrice = item.basePrice;
            result.totalDiscount = 0;
            return result;
        }

        if (stackDiscounts) {
            // All discounts stack - apply each in sequence
            uint32_t currentPrice = item.basePrice;
            for (const auto& [discountInfo, name] : applicableDiscounts) {
                uint32_t discountAmount = discountInfo->calculateDiscount(currentPrice);
                currentPrice = (currentPrice > discountAmount) ? (currentPrice - discountAmount) : 0;
                result.appliedDiscountNames.push_back(name);
            }
            result.totalDiscount = item.basePrice - currentPrice;
            result.finalPrice = currentPrice;
        } else {
            // Use best (largest) discount only
            uint32_t bestDiscount = 0;
            std::string bestDiscountName;

            for (const auto& [discountInfo, name] : applicableDiscounts) {
                uint32_t discountAmount = discountInfo->calculateDiscount(item.basePrice);
                if (discountAmount > bestDiscount) {
                    bestDiscount = discountAmount;
                    bestDiscountName = name;
                }
            }

            result.totalDiscount = bestDiscount;
            result.finalPrice = (item.basePrice > bestDiscount) ? (item.basePrice - bestDiscount) : 0;
            if (!bestDiscountName.empty()) {
                result.appliedDiscountNames.push_back(bestDiscountName);
            }
        }

        return result;
    }

    /**
     * Get all priced items given current time conditions
     */
    std::vector<PricedItem> getAllPricedItems(
        float timeOfDay,
        DayOfWeek dayOfWeek,
        int weekOfMonth,
        bool stackDiscounts = false
    ) const {
        std::vector<PricedItem> result;
        result.reserve(items.size());
        for (const auto& item : items) {
            result.push_back(calculatePrice(item, timeOfDay, dayOfWeek, weekOfMonth, stackDiscounts));
        }
        return result;
    }

    /**
     * Get names of all currently active discounts
     */
    std::vector<std::string> getActiveDiscountNames(
        float timeOfDay,
        DayOfWeek dayOfWeek,
        int weekOfMonth
    ) const {
        std::vector<std::string> names;

        for (const auto& discount : happyHourDiscounts) {
            if (discount.isActive(timeOfDay, dayOfWeek)) {
                names.push_back(discount.info.name);
            }
        }

        for (const auto& discount : dailyDiscounts) {
            if (discount.isActive(dayOfWeek)) {
                names.push_back(discount.info.name);
            }
        }

        for (const auto& discount : weeklyDiscounts) {
            if (discount.isActive(weekOfMonth)) {
                names.push_back(discount.info.name);
            }
        }

        return names;
    }

private:
    void rebuildIndex() {
        itemIndex.clear();
        for (size_t i = 0; i < items.size(); ++i) {
            itemIndex[items[i].id] = i;
        }
    }

    std::string menuName;
    std::string establishmentName;
    std::vector<MenuItem> items;
    std::unordered_map<std::string, size_t> itemIndex;

    std::vector<HappyHourDiscount> happyHourDiscounts;
    std::vector<DailyDiscount> dailyDiscounts;
    std::vector<WeeklyDiscount> weeklyDiscounts;
};

} // namespace economy
