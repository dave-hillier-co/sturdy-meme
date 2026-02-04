#pragma once

#include "Menu.h"
#include "Discount.h"
#include "core/interfaces/ITimeSystem.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace economy {

/**
 * PricingSystem - Central system for managing menus and calculating prices
 *
 * Integrates with the TimeSystem to automatically apply time-based discounts.
 * Manages multiple menus (e.g., for different taverns/shops in the game).
 *
 * Usage:
 *   PricingSystem pricing;
 *   pricing.setTimeSystem(&timeSystem);
 *   pricing.registerMenu("tavern_01", tavernMenu);
 *
 *   // Get prices with current time
 *   auto price = pricing.getPrice("tavern_01", "ale_common");
 */
class PricingSystem {
public:
    PricingSystem() = default;

    /**
     * Set the time system reference for automatic time-based pricing
     */
    void setTimeSystem(ITimeSystem* system) { timeSystem = system; }

    /**
     * Register a menu with a unique identifier
     */
    void registerMenu(const std::string& menuId, const Menu& menu) {
        menus[menuId] = menu;
    }

    /**
     * Get a menu by ID
     */
    Menu* getMenu(const std::string& menuId) {
        auto it = menus.find(menuId);
        return (it != menus.end()) ? &it->second : nullptr;
    }

    const Menu* getMenu(const std::string& menuId) const {
        auto it = menus.find(menuId);
        return (it != menus.end()) ? &it->second : nullptr;
    }

    /**
     * Remove a menu
     */
    void unregisterMenu(const std::string& menuId) {
        menus.erase(menuId);
    }

    /**
     * Get all registered menu IDs
     */
    std::vector<std::string> getMenuIds() const {
        std::vector<std::string> ids;
        ids.reserve(menus.size());
        for (const auto& [id, menu] : menus) {
            ids.push_back(id);
        }
        return ids;
    }

    /**
     * Get the current time context from TimeSystem
     */
    struct TimeContext {
        float timeOfDay = 0.5f;
        DayOfWeek dayOfWeek = DayOfWeek::Monday;
        int weekOfMonth = 1;
    };

    TimeContext getCurrentTimeContext() const {
        TimeContext ctx;

        if (timeSystem) {
            ctx.timeOfDay = timeSystem->getTimeOfDay();

            int year = timeSystem->getCurrentYear();
            int month = timeSystem->getCurrentMonth();
            int day = timeSystem->getCurrentDay();

            ctx.dayOfWeek = calculateDayOfWeek(year, month, day);
            ctx.weekOfMonth = economy::weekOfMonth(day);
        }

        return ctx;
    }

    /**
     * Get the price for an item using current time from TimeSystem
     *
     * @param menuId The menu containing the item
     * @param itemId The item to price
     * @param stackDiscounts Whether to stack multiple discounts
     */
    PricedItem getPrice(
        const std::string& menuId,
        const std::string& itemId,
        bool stackDiscounts = false
    ) const {
        const Menu* menu = getMenu(menuId);
        if (!menu) {
            return PricedItem{};
        }

        auto ctx = getCurrentTimeContext();
        return menu->calculatePrice(itemId, ctx.timeOfDay, ctx.dayOfWeek, ctx.weekOfMonth, stackDiscounts);
    }

    /**
     * Get all priced items from a menu using current time
     */
    std::vector<PricedItem> getAllPrices(
        const std::string& menuId,
        bool stackDiscounts = false
    ) const {
        const Menu* menu = getMenu(menuId);
        if (!menu) {
            return {};
        }

        auto ctx = getCurrentTimeContext();
        return menu->getAllPricedItems(ctx.timeOfDay, ctx.dayOfWeek, ctx.weekOfMonth, stackDiscounts);
    }

    /**
     * Get currently active discount names for a menu
     */
    std::vector<std::string> getActiveDiscounts(const std::string& menuId) const {
        const Menu* menu = getMenu(menuId);
        if (!menu) {
            return {};
        }

        auto ctx = getCurrentTimeContext();
        return menu->getActiveDiscountNames(ctx.timeOfDay, ctx.dayOfWeek, ctx.weekOfMonth);
    }

    /**
     * Get price with explicit time context (for testing or previewing)
     */
    PricedItem getPriceAt(
        const std::string& menuId,
        const std::string& itemId,
        float timeOfDay,
        DayOfWeek dayOfWeek,
        int weekOfMonth,
        bool stackDiscounts = false
    ) const {
        const Menu* menu = getMenu(menuId);
        if (!menu) {
            return PricedItem{};
        }

        return menu->calculatePrice(itemId, timeOfDay, dayOfWeek, weekOfMonth, stackDiscounts);
    }

    /**
     * Configuration for discount stacking behavior
     */
    void setDefaultStackDiscounts(bool stack) { defaultStackDiscounts = stack; }
    bool getDefaultStackDiscounts() const { return defaultStackDiscounts; }

private:
    ITimeSystem* timeSystem = nullptr;
    std::unordered_map<std::string, Menu> menus;
    bool defaultStackDiscounts = false;
};

/**
 * Factory functions to create common discount configurations
 */
namespace DiscountFactory {

/**
 * Create a standard happy hour discount (e.g., 4pm-6pm, 20% off drinks)
 */
inline HappyHourDiscount createHappyHour(
    const std::string& name,
    int startHour,
    int endHour,
    uint32_t percentOff,
    MenuCategory category = MenuCategory::Drink
) {
    HappyHourDiscount discount;
    discount.info
        .setId("happy_hour_" + name)
        .setName(name)
        .setType(DiscountType::Percentage)
        .setValue(percentOff)
        .setTargetCategory(category);
    discount.setStartHour(startHour).setEndHour(endHour);
    return discount;
}

/**
 * Create a day-of-week special (e.g., "Mead Monday")
 */
inline DailyDiscount createDailySpecial(
    const std::string& name,
    DayOfWeek day,
    uint32_t percentOff,
    const std::vector<std::string>& itemIds = {}
) {
    DailyDiscount discount;
    discount.info
        .setId("daily_" + name)
        .setName(name)
        .setType(DiscountType::Percentage)
        .setValue(percentOff);

    if (itemIds.empty()) {
        discount.info.setTargetAllItems();
    } else {
        discount.info.setTargetItems(itemIds);
    }

    discount.addActiveDay(day);
    return discount;
}

/**
 * Create a week-of-month special (e.g., "First Week Feast")
 */
inline WeeklyDiscount createWeeklySpecial(
    const std::string& name,
    int weekOfMonth,
    uint32_t percentOff,
    MenuCategory category = MenuCategory::Food
) {
    WeeklyDiscount discount;
    discount.info
        .setId("weekly_" + name)
        .setName(name)
        .setType(DiscountType::Percentage)
        .setValue(percentOff)
        .setTargetCategory(category);
    discount.addActiveWeek(weekOfMonth);
    return discount;
}

/**
 * Create a late-night discount (e.g., after 10pm)
 */
inline HappyHourDiscount createLateNightSpecial(
    const std::string& name,
    uint32_t percentOff
) {
    HappyHourDiscount discount;
    discount.info
        .setId("late_night_" + name)
        .setName(name)
        .setType(DiscountType::Percentage)
        .setValue(percentOff)
        .setTargetAllItems();
    discount.setStartHour(22).setEndHour(2);  // 10pm to 2am
    return discount;
}

/**
 * Create a weekend special (Saturday and Sunday)
 */
inline DailyDiscount createWeekendSpecial(
    const std::string& name,
    uint32_t percentOff,
    MenuCategory category = MenuCategory::Food
) {
    DailyDiscount discount;
    discount.info
        .setId("weekend_" + name)
        .setName(name)
        .setType(DiscountType::Percentage)
        .setValue(percentOff)
        .setTargetCategory(category);
    discount.addActiveDay(DayOfWeek::Saturday);
    discount.addActiveDay(DayOfWeek::Sunday);
    return discount;
}

} // namespace DiscountFactory

/**
 * Create a sample tavern menu with typical items and discounts
 * Useful for testing and as an example
 */
inline Menu createSampleTavernMenu() {
    Menu menu("The Rusty Tankard Menu");
    menu.setEstablishment("The Rusty Tankard");

    // Add drinks
    menu.addItem(MenuItem()
        .setId("ale_common")
        .setName("Common Ale")
        .setDescription("A hearty local brew")
        .setCategory(MenuCategory::Drink)
        .setBasePrice(50));  // 50 copper

    menu.addItem(MenuItem()
        .setId("ale_premium")
        .setName("Dwarven Stout")
        .setDescription("Strong and dark, imported from the mountain halls")
        .setCategory(MenuCategory::Drink)
        .setBasePrice(150));

    menu.addItem(MenuItem()
        .setId("mead")
        .setName("Honey Mead")
        .setDescription("Sweet mead made with local wildflower honey")
        .setCategory(MenuCategory::Drink)
        .setBasePrice(100));

    menu.addItem(MenuItem()
        .setId("wine_house")
        .setName("House Wine")
        .setDescription("A respectable red from the southern vineyards")
        .setCategory(MenuCategory::Drink)
        .setBasePrice(200));

    // Add food
    menu.addItem(MenuItem()
        .setId("stew")
        .setName("Hearty Stew")
        .setDescription("Thick beef stew with root vegetables")
        .setCategory(MenuCategory::Food)
        .setBasePrice(100));

    menu.addItem(MenuItem()
        .setId("bread_cheese")
        .setName("Bread and Cheese")
        .setDescription("Fresh bread with aged cheese")
        .setCategory(MenuCategory::Food)
        .setBasePrice(60));

    menu.addItem(MenuItem()
        .setId("roast")
        .setName("Roasted Chicken")
        .setDescription("Half a chicken with herbs and potatoes")
        .setCategory(MenuCategory::Food)
        .setBasePrice(250));

    menu.addItem(MenuItem()
        .setId("pie_meat")
        .setName("Meat Pie")
        .setDescription("Flaky pastry filled with seasoned pork")
        .setCategory(MenuCategory::Food)
        .setBasePrice(120));

    // Add desserts
    menu.addItem(MenuItem()
        .setId("pudding")
        .setName("Bread Pudding")
        .setDescription("Warm bread pudding with honey glaze")
        .setCategory(MenuCategory::Dessert)
        .setBasePrice(80));

    // Add discounts
    // Happy hour: 4pm-6pm, 20% off drinks
    menu.addHappyHourDiscount(
        DiscountFactory::createHappyHour("Happy Hour", 16, 18, 20, MenuCategory::Drink));

    // Mead Monday: 15% off mead on Mondays
    menu.addDailyDiscount(
        DiscountFactory::createDailySpecial("Mead Monday", DayOfWeek::Monday, 15, {"mead"}));

    // Thirsty Thursday: 10% off all drinks
    DailyDiscount thirstyThursday;
    thirstyThursday.info
        .setId("thirsty_thursday")
        .setName("Thirsty Thursday")
        .setType(DiscountType::Percentage)
        .setValue(10)
        .setTargetCategory(MenuCategory::Drink);
    thirstyThursday.addActiveDay(DayOfWeek::Thursday);
    menu.addDailyDiscount(thirstyThursday);

    // Weekend Feast: 10% off food on weekends
    menu.addDailyDiscount(
        DiscountFactory::createWeekendSpecial("Weekend Feast", 10, MenuCategory::Food));

    // First Week Special: 15% off food in first week of month
    menu.addWeeklyDiscount(
        DiscountFactory::createWeeklySpecial("First Week Feast", 1, 15, MenuCategory::Food));

    // Late night special: 25% off everything after 10pm
    menu.addHappyHourDiscount(
        DiscountFactory::createLateNightSpecial("Night Owl Special", 25));

    return menu;
}

} // namespace economy
