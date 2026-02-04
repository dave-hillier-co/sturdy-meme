#pragma once

#include "MenuItem.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace economy {

/**
 * Days of the week for scheduling discounts
 */
enum class DayOfWeek {
    Sunday = 0,
    Monday = 1,
    Tuesday = 2,
    Wednesday = 3,
    Thursday = 4,
    Friday = 5,
    Saturday = 6
};

/**
 * How the discount is applied
 */
enum class DiscountType {
    Percentage,  // e.g., 20% off
    FixedAmount  // e.g., 50 copper off
};

/**
 * What items the discount applies to
 */
enum class DiscountTarget {
    AllItems,           // Applies to everything
    Category,           // Applies to specific category
    SpecificItems       // Applies to specific item IDs
};

/**
 * Base structure for all discount information
 */
struct DiscountInfo {
    std::string id;           // Unique identifier
    std::string name;         // Display name (e.g., "Happy Hour Special")
    std::string description;  // Description for display

    DiscountType type = DiscountType::Percentage;
    uint32_t value = 0;       // Percentage (0-100) or fixed copper amount

    DiscountTarget target = DiscountTarget::AllItems;
    std::optional<MenuCategory> targetCategory;  // If target is Category
    std::vector<std::string> targetItemIds;      // If target is SpecificItems

    // Builder-style setters
    DiscountInfo& setId(const std::string& val) { id = val; return *this; }
    DiscountInfo& setName(const std::string& val) { name = val; return *this; }
    DiscountInfo& setDescription(const std::string& val) { description = val; return *this; }
    DiscountInfo& setType(DiscountType val) { type = val; return *this; }
    DiscountInfo& setValue(uint32_t val) { value = val; return *this; }
    DiscountInfo& setTargetAllItems() {
        target = DiscountTarget::AllItems;
        targetCategory.reset();
        targetItemIds.clear();
        return *this;
    }
    DiscountInfo& setTargetCategory(MenuCategory cat) {
        target = DiscountTarget::Category;
        targetCategory = cat;
        targetItemIds.clear();
        return *this;
    }
    DiscountInfo& setTargetItems(const std::vector<std::string>& ids) {
        target = DiscountTarget::SpecificItems;
        targetCategory.reset();
        targetItemIds = ids;
        return *this;
    }

    /**
     * Check if this discount applies to a given menu item
     */
    bool appliesTo(const MenuItem& item) const {
        switch (target) {
            case DiscountTarget::AllItems:
                return true;
            case DiscountTarget::Category:
                return targetCategory.has_value() && item.category == *targetCategory;
            case DiscountTarget::SpecificItems:
                for (const auto& targetId : targetItemIds) {
                    if (item.id == targetId) return true;
                }
                return false;
        }
        return false;
    }

    /**
     * Calculate the discounted price for an item
     * Returns the discount amount (how much is subtracted)
     */
    uint32_t calculateDiscount(uint32_t basePrice) const {
        if (type == DiscountType::Percentage) {
            return (basePrice * value) / 100;
        } else {
            // Fixed amount discount, but don't go below zero
            return std::min(value, basePrice);
        }
    }
};

/**
 * Happy Hour discount - active during specific hours of the day
 *
 * Uses normalized time-of-day (0.0 = midnight, 0.5 = noon, 1.0 = midnight)
 * to match the TimeSystem's timeOfDay format.
 */
struct HappyHourDiscount {
    DiscountInfo info;

    float startTime = 0.0f;   // Normalized time (0-1), e.g., 0.667 = 4pm
    float endTime = 0.0f;     // Normalized time (0-1), e.g., 0.75 = 6pm

    // Optional: only active on certain days
    std::vector<DayOfWeek> activeDays;  // Empty = active every day

    /**
     * Check if the discount is currently active
     * @param currentTimeOfDay Normalized time (0-1) from TimeSystem
     * @param currentDay Optional day of week check
     */
    bool isActive(float currentTimeOfDay, std::optional<DayOfWeek> currentDay = std::nullopt) const {
        // Check day of week if specified
        if (!activeDays.empty() && currentDay.has_value()) {
            bool dayMatch = false;
            for (auto day : activeDays) {
                if (day == *currentDay) {
                    dayMatch = true;
                    break;
                }
            }
            if (!dayMatch) return false;
        }

        // Handle time range (supports wrap-around midnight)
        if (startTime <= endTime) {
            return currentTimeOfDay >= startTime && currentTimeOfDay < endTime;
        } else {
            // Wraps around midnight (e.g., 10pm to 2am)
            return currentTimeOfDay >= startTime || currentTimeOfDay < endTime;
        }
    }

    // Builder-style setters
    HappyHourDiscount& setStartTime(float t) { startTime = t; return *this; }
    HappyHourDiscount& setEndTime(float t) { endTime = t; return *this; }
    HappyHourDiscount& setActiveDays(const std::vector<DayOfWeek>& days) { activeDays = days; return *this; }

    // Convenience: set time from hours (0-24)
    HappyHourDiscount& setStartHour(int hour) { startTime = static_cast<float>(hour) / 24.0f; return *this; }
    HappyHourDiscount& setEndHour(int hour) { endTime = static_cast<float>(hour) / 24.0f; return *this; }
};

/**
 * Daily discount - active on specific days of the week
 *
 * e.g., "Mead Monday", "Thirsty Thursday"
 */
struct DailyDiscount {
    DiscountInfo info;

    std::vector<DayOfWeek> activeDays;  // Days when discount is active

    /**
     * Check if discount is active on the given day
     */
    bool isActive(DayOfWeek currentDay) const {
        for (auto day : activeDays) {
            if (day == currentDay) return true;
        }
        return false;
    }

    // Builder-style setters
    DailyDiscount& setActiveDays(const std::vector<DayOfWeek>& days) { activeDays = days; return *this; }
    DailyDiscount& addActiveDay(DayOfWeek day) { activeDays.push_back(day); return *this; }
};

/**
 * Weekly discount - active during specific weeks of the month
 *
 * e.g., "First week of the month special"
 */
struct WeeklyDiscount {
    DiscountInfo info;

    std::vector<int> activeWeeks;  // 1-5 (week of month, 1 = first week)

    /**
     * Check if discount is active in the given week of month
     */
    bool isActive(int weekOfMonth) const {
        for (auto week : activeWeeks) {
            if (week == weekOfMonth) return true;
        }
        return false;
    }

    // Builder-style setters
    WeeklyDiscount& setActiveWeeks(const std::vector<int>& weeks) { activeWeeks = weeks; return *this; }
    WeeklyDiscount& addActiveWeek(int week) { activeWeeks.push_back(week); return *this; }
};

/**
 * Helper to convert day number (0-6 or from calendar) to DayOfWeek enum
 */
inline DayOfWeek dayOfWeekFromNumber(int dayNum) {
    return static_cast<DayOfWeek>(dayNum % 7);
}

/**
 * Calculate week of month from day of month (1-31)
 * Returns 1-5
 */
inline int weekOfMonth(int dayOfMonth) {
    return ((dayOfMonth - 1) / 7) + 1;
}

/**
 * Calculate day of week using Zeller's congruence
 * Returns 0 = Sunday, 1 = Monday, ... 6 = Saturday
 */
inline DayOfWeek calculateDayOfWeek(int year, int month, int day) {
    // Adjust for Zeller's: January=13, February=14 of previous year
    if (month < 3) {
        month += 12;
        year--;
    }

    int q = day;
    int m = month;
    int k = year % 100;
    int j = year / 100;

    int h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    // Convert from Zeller's result (0=Sat, 1=Sun, 2=Mon...) to our enum (0=Sun)
    int dow = ((h + 6) % 7);
    return static_cast<DayOfWeek>(dow);
}

} // namespace economy
