#include <doctest/doctest.h>
#include "economy/MenuItem.h"
#include "economy/Discount.h"
#include "economy/Menu.h"
#include "economy/PricingSystem.h"

using namespace economy;

TEST_CASE("MenuItem basic operations") {
    MenuItem item;
    item.setId("test_ale")
        .setName("Test Ale")
        .setDescription("A test beverage")
        .setCategory(MenuCategory::Drink)
        .setBasePrice(100);

    CHECK(item.id == "test_ale");
    CHECK(item.name == "Test Ale");
    CHECK(item.category == MenuCategory::Drink);
    CHECK(item.basePrice == 100);
    CHECK(item.available == true);
}

TEST_CASE("formatPrice helper") {
    CHECK(formatPrice(50) == "0s 50c");
    CHECK(formatPrice(100) == "1s 0c");
    CHECK(formatPrice(150) == "1s 50c");
    CHECK(formatPrice(1234) == "12s 34c");
    CHECK(formatPrice(10000) == "1g 0s 0c");
    CHECK(formatPrice(12345) == "1g 23s 45c");
}

TEST_CASE("DayOfWeek calculation") {
    // Known dates for verification
    // January 1, 2024 was a Monday
    CHECK(calculateDayOfWeek(2024, 1, 1) == DayOfWeek::Monday);

    // December 25, 2024 is a Wednesday
    CHECK(calculateDayOfWeek(2024, 12, 25) == DayOfWeek::Wednesday);

    // July 4, 2024 is a Thursday
    CHECK(calculateDayOfWeek(2024, 7, 4) == DayOfWeek::Thursday);

    // February 29, 2024 (leap year) is a Thursday
    CHECK(calculateDayOfWeek(2024, 2, 29) == DayOfWeek::Thursday);
}

TEST_CASE("weekOfMonth calculation") {
    CHECK(weekOfMonth(1) == 1);
    CHECK(weekOfMonth(7) == 1);
    CHECK(weekOfMonth(8) == 2);
    CHECK(weekOfMonth(14) == 2);
    CHECK(weekOfMonth(15) == 3);
    CHECK(weekOfMonth(21) == 3);
    CHECK(weekOfMonth(22) == 4);
    CHECK(weekOfMonth(28) == 4);
    CHECK(weekOfMonth(29) == 5);
    CHECK(weekOfMonth(31) == 5);
}

TEST_CASE("DiscountInfo applies to items correctly") {
    MenuItem drinkItem;
    drinkItem.setId("ale").setCategory(MenuCategory::Drink);

    MenuItem foodItem;
    foodItem.setId("stew").setCategory(MenuCategory::Food);

    SUBCASE("AllItems target") {
        DiscountInfo discount;
        discount.setTargetAllItems();

        CHECK(discount.appliesTo(drinkItem) == true);
        CHECK(discount.appliesTo(foodItem) == true);
    }

    SUBCASE("Category target") {
        DiscountInfo discount;
        discount.setTargetCategory(MenuCategory::Drink);

        CHECK(discount.appliesTo(drinkItem) == true);
        CHECK(discount.appliesTo(foodItem) == false);
    }

    SUBCASE("Specific items target") {
        DiscountInfo discount;
        discount.setTargetItems({"ale", "mead"});

        CHECK(discount.appliesTo(drinkItem) == true);
        CHECK(discount.appliesTo(foodItem) == false);
    }
}

TEST_CASE("DiscountInfo calculateDiscount") {
    SUBCASE("Percentage discount") {
        DiscountInfo discount;
        discount.setType(DiscountType::Percentage).setValue(20);

        CHECK(discount.calculateDiscount(100) == 20);
        CHECK(discount.calculateDiscount(150) == 30);
        CHECK(discount.calculateDiscount(50) == 10);
    }

    SUBCASE("Fixed amount discount") {
        DiscountInfo discount;
        discount.setType(DiscountType::FixedAmount).setValue(25);

        CHECK(discount.calculateDiscount(100) == 25);
        CHECK(discount.calculateDiscount(50) == 25);
        CHECK(discount.calculateDiscount(20) == 20);  // Capped at base price
    }
}

TEST_CASE("HappyHourDiscount isActive") {
    HappyHourDiscount discount;
    // 4pm (0.667) to 6pm (0.75)
    discount.setStartHour(16).setEndHour(18);

    SUBCASE("Within happy hour") {
        CHECK(discount.isActive(0.7f) == true);   // ~5pm
        CHECK(discount.isActive(0.67f) == true);  // ~4pm
    }

    SUBCASE("Outside happy hour") {
        CHECK(discount.isActive(0.5f) == false);  // Noon
        CHECK(discount.isActive(0.8f) == false);  // ~7pm
        CHECK(discount.isActive(0.0f) == false);  // Midnight
    }

    SUBCASE("Wrap around midnight") {
        HappyHourDiscount lateNight;
        lateNight.setStartHour(22).setEndHour(2);  // 10pm to 2am

        CHECK(lateNight.isActive(0.95f) == true);   // ~11pm
        CHECK(lateNight.isActive(0.05f) == true);   // ~1am
        CHECK(lateNight.isActive(0.5f) == false);   // Noon
        CHECK(lateNight.isActive(0.75f) == false);  // 6pm
    }

    SUBCASE("Day restriction") {
        HappyHourDiscount weekdayOnly;
        weekdayOnly.setStartHour(16).setEndHour(18);
        weekdayOnly.setActiveDays({DayOfWeek::Monday, DayOfWeek::Tuesday,
                                   DayOfWeek::Wednesday, DayOfWeek::Thursday,
                                   DayOfWeek::Friday});

        float happyHourTime = 0.7f;  // ~5pm

        CHECK(weekdayOnly.isActive(happyHourTime, DayOfWeek::Monday) == true);
        CHECK(weekdayOnly.isActive(happyHourTime, DayOfWeek::Saturday) == false);
        CHECK(weekdayOnly.isActive(happyHourTime, DayOfWeek::Sunday) == false);
    }
}

TEST_CASE("DailyDiscount isActive") {
    DailyDiscount meadMonday;
    meadMonday.addActiveDay(DayOfWeek::Monday);

    CHECK(meadMonday.isActive(DayOfWeek::Monday) == true);
    CHECK(meadMonday.isActive(DayOfWeek::Tuesday) == false);
    CHECK(meadMonday.isActive(DayOfWeek::Sunday) == false);

    DailyDiscount weekend;
    weekend.addActiveDay(DayOfWeek::Saturday);
    weekend.addActiveDay(DayOfWeek::Sunday);

    CHECK(weekend.isActive(DayOfWeek::Saturday) == true);
    CHECK(weekend.isActive(DayOfWeek::Sunday) == true);
    CHECK(weekend.isActive(DayOfWeek::Friday) == false);
}

TEST_CASE("WeeklyDiscount isActive") {
    WeeklyDiscount firstWeek;
    firstWeek.addActiveWeek(1);

    CHECK(firstWeek.isActive(1) == true);
    CHECK(firstWeek.isActive(2) == false);
    CHECK(firstWeek.isActive(5) == false);

    WeeklyDiscount firstAndLast;
    firstAndLast.addActiveWeek(1);
    firstAndLast.addActiveWeek(5);

    CHECK(firstAndLast.isActive(1) == true);
    CHECK(firstAndLast.isActive(5) == true);
    CHECK(firstAndLast.isActive(3) == false);
}

TEST_CASE("Menu item management") {
    Menu menu("Test Menu");

    MenuItem ale;
    ale.setId("ale").setName("Ale").setBasePrice(50);

    MenuItem stew;
    stew.setId("stew").setName("Stew").setBasePrice(100);

    menu.addItem(ale);
    menu.addItem(stew);

    CHECK(menu.getItems().size() == 2);
    CHECK(menu.getItem("ale") != nullptr);
    CHECK(menu.getItem("ale")->name == "Ale");
    CHECK(menu.getItem("nonexistent") == nullptr);

    menu.removeItem("ale");
    CHECK(menu.getItems().size() == 1);
    CHECK(menu.getItem("ale") == nullptr);
}

TEST_CASE("Menu price calculation") {
    Menu menu("Test Menu");

    MenuItem ale;
    ale.setId("ale").setName("Ale").setCategory(MenuCategory::Drink).setBasePrice(100);
    menu.addItem(ale);

    MenuItem stew;
    stew.setId("stew").setName("Stew").setCategory(MenuCategory::Food).setBasePrice(200);
    menu.addItem(stew);

    SUBCASE("No discounts") {
        auto priced = menu.calculatePrice("ale", 0.5f, DayOfWeek::Monday, 1);

        CHECK(priced.item != nullptr);
        CHECK(priced.basePrice == 100);
        CHECK(priced.finalPrice == 100);
        CHECK(priced.totalDiscount == 0);
        CHECK(priced.hasDiscount() == false);
    }

    SUBCASE("Happy hour discount applies") {
        HappyHourDiscount happyHour;
        happyHour.info
            .setName("Happy Hour")
            .setType(DiscountType::Percentage)
            .setValue(20)
            .setTargetCategory(MenuCategory::Drink);
        happyHour.setStartHour(16).setEndHour(18);
        menu.addHappyHourDiscount(happyHour);

        // During happy hour
        auto priced = menu.calculatePrice("ale", 0.7f, DayOfWeek::Monday, 1);
        CHECK(priced.finalPrice == 80);
        CHECK(priced.totalDiscount == 20);
        CHECK(priced.hasDiscount() == true);

        // Food should not be discounted
        auto foodPriced = menu.calculatePrice("stew", 0.7f, DayOfWeek::Monday, 1);
        CHECK(foodPriced.finalPrice == 200);
        CHECK(foodPriced.hasDiscount() == false);

        // Outside happy hour
        auto outsidePriced = menu.calculatePrice("ale", 0.5f, DayOfWeek::Monday, 1);
        CHECK(outsidePriced.finalPrice == 100);
        CHECK(outsidePriced.hasDiscount() == false);
    }

    SUBCASE("Daily discount applies") {
        DailyDiscount meadMonday;
        meadMonday.info
            .setName("Mead Monday")
            .setType(DiscountType::Percentage)
            .setValue(15)
            .setTargetItems({"ale"});
        meadMonday.addActiveDay(DayOfWeek::Monday);
        menu.addDailyDiscount(meadMonday);

        // On Monday
        auto priced = menu.calculatePrice("ale", 0.5f, DayOfWeek::Monday, 1);
        CHECK(priced.finalPrice == 85);
        CHECK(priced.totalDiscount == 15);

        // On Tuesday
        auto tuesdayPriced = menu.calculatePrice("ale", 0.5f, DayOfWeek::Tuesday, 1);
        CHECK(tuesdayPriced.finalPrice == 100);
        CHECK(tuesdayPriced.hasDiscount() == false);
    }

    SUBCASE("Weekly discount applies") {
        WeeklyDiscount firstWeek;
        firstWeek.info
            .setName("First Week Feast")
            .setType(DiscountType::Percentage)
            .setValue(10)
            .setTargetCategory(MenuCategory::Food);
        firstWeek.addActiveWeek(1);
        menu.addWeeklyDiscount(firstWeek);

        // First week
        auto priced = menu.calculatePrice("stew", 0.5f, DayOfWeek::Monday, 1);
        CHECK(priced.finalPrice == 180);
        CHECK(priced.totalDiscount == 20);

        // Second week
        auto week2Priced = menu.calculatePrice("stew", 0.5f, DayOfWeek::Monday, 2);
        CHECK(week2Priced.finalPrice == 200);
        CHECK(week2Priced.hasDiscount() == false);
    }

    SUBCASE("Best discount selected (no stacking)") {
        // Add two overlapping discounts
        HappyHourDiscount happyHour;
        happyHour.info
            .setName("Happy Hour 20%")
            .setType(DiscountType::Percentage)
            .setValue(20)
            .setTargetAllItems();
        happyHour.setStartHour(16).setEndHour(18);
        menu.addHappyHourDiscount(happyHour);

        DailyDiscount dailySpecial;
        dailySpecial.info
            .setName("Daily Special 10%")
            .setType(DiscountType::Percentage)
            .setValue(10)
            .setTargetAllItems();
        dailySpecial.addActiveDay(DayOfWeek::Monday);
        menu.addDailyDiscount(dailySpecial);

        // Both active, should use best (20%)
        auto priced = menu.calculatePrice("ale", 0.7f, DayOfWeek::Monday, 1, false);
        CHECK(priced.finalPrice == 80);
        CHECK(priced.totalDiscount == 20);
        CHECK(priced.appliedDiscountNames.size() == 1);
        CHECK(priced.appliedDiscountNames[0] == "Happy Hour 20%");
    }

    SUBCASE("Stacking discounts") {
        HappyHourDiscount happyHour;
        happyHour.info
            .setName("Happy Hour 20%")
            .setType(DiscountType::Percentage)
            .setValue(20)
            .setTargetAllItems();
        happyHour.setStartHour(16).setEndHour(18);
        menu.addHappyHourDiscount(happyHour);

        DailyDiscount dailySpecial;
        dailySpecial.info
            .setName("Daily Special 10%")
            .setType(DiscountType::Percentage)
            .setValue(10)
            .setTargetAllItems();
        dailySpecial.addActiveDay(DayOfWeek::Monday);
        menu.addDailyDiscount(dailySpecial);

        // Both active with stacking
        // Base: 100, after 20%: 80, after 10% of 80: 72
        auto priced = menu.calculatePrice("ale", 0.7f, DayOfWeek::Monday, 1, true);
        CHECK(priced.finalPrice == 72);
        CHECK(priced.totalDiscount == 28);
        CHECK(priced.appliedDiscountNames.size() == 2);
    }
}

TEST_CASE("Menu getActiveDiscountNames") {
    Menu menu("Test Menu");

    HappyHourDiscount happyHour;
    happyHour.info.setName("Happy Hour");
    happyHour.setStartHour(16).setEndHour(18);
    menu.addHappyHourDiscount(happyHour);

    DailyDiscount dailySpecial;
    dailySpecial.info.setName("Monday Special");
    dailySpecial.addActiveDay(DayOfWeek::Monday);
    menu.addDailyDiscount(dailySpecial);

    // During happy hour on Monday
    auto names = menu.getActiveDiscountNames(0.7f, DayOfWeek::Monday, 1);
    CHECK(names.size() == 2);

    // Outside happy hour on Monday
    names = menu.getActiveDiscountNames(0.5f, DayOfWeek::Monday, 1);
    CHECK(names.size() == 1);
    CHECK(names[0] == "Monday Special");

    // During happy hour on Tuesday
    names = menu.getActiveDiscountNames(0.7f, DayOfWeek::Tuesday, 1);
    CHECK(names.size() == 1);
    CHECK(names[0] == "Happy Hour");
}

TEST_CASE("PricingSystem basic operations") {
    PricingSystem pricing;

    Menu tavernMenu("Tavern");
    MenuItem ale;
    ale.setId("ale").setName("Ale").setBasePrice(100);
    tavernMenu.addItem(ale);

    pricing.registerMenu("tavern_01", tavernMenu);

    CHECK(pricing.getMenu("tavern_01") != nullptr);
    CHECK(pricing.getMenu("nonexistent") == nullptr);

    auto ids = pricing.getMenuIds();
    CHECK(ids.size() == 1);
    CHECK(ids[0] == "tavern_01");

    pricing.unregisterMenu("tavern_01");
    CHECK(pricing.getMenu("tavern_01") == nullptr);
}

TEST_CASE("PricingSystem getPriceAt") {
    PricingSystem pricing;

    Menu menu("Test");
    MenuItem ale;
    ale.setId("ale").setCategory(MenuCategory::Drink).setBasePrice(100);
    menu.addItem(ale);

    HappyHourDiscount happyHour;
    happyHour.info
        .setName("Happy Hour")
        .setType(DiscountType::Percentage)
        .setValue(20)
        .setTargetCategory(MenuCategory::Drink);
    happyHour.setStartHour(16).setEndHour(18);
    menu.addHappyHourDiscount(happyHour);

    pricing.registerMenu("test", menu);

    // Test explicit time context
    auto priced = pricing.getPriceAt("test", "ale", 0.7f, DayOfWeek::Monday, 1);
    CHECK(priced.finalPrice == 80);

    priced = pricing.getPriceAt("test", "ale", 0.5f, DayOfWeek::Monday, 1);
    CHECK(priced.finalPrice == 100);
}

TEST_CASE("DiscountFactory helpers") {
    SUBCASE("createHappyHour") {
        auto discount = DiscountFactory::createHappyHour("Test Happy Hour", 16, 18, 20);
        CHECK(discount.info.name == "Test Happy Hour");
        CHECK(discount.info.value == 20);
        CHECK(discount.info.type == DiscountType::Percentage);
        CHECK(discount.isActive(0.7f) == true);
        CHECK(discount.isActive(0.5f) == false);
    }

    SUBCASE("createDailySpecial") {
        auto discount = DiscountFactory::createDailySpecial("Monday Madness", DayOfWeek::Monday, 15);
        CHECK(discount.info.name == "Monday Madness");
        CHECK(discount.info.value == 15);
        CHECK(discount.isActive(DayOfWeek::Monday) == true);
        CHECK(discount.isActive(DayOfWeek::Tuesday) == false);
    }

    SUBCASE("createWeeklySpecial") {
        auto discount = DiscountFactory::createWeeklySpecial("First Week", 1, 10);
        CHECK(discount.info.name == "First Week");
        CHECK(discount.info.value == 10);
        CHECK(discount.isActive(1) == true);
        CHECK(discount.isActive(2) == false);
    }

    SUBCASE("createLateNightSpecial") {
        auto discount = DiscountFactory::createLateNightSpecial("Night Owl", 25);
        CHECK(discount.info.name == "Night Owl");
        CHECK(discount.info.value == 25);
        CHECK(discount.isActive(0.95f) == true);   // 11pm
        CHECK(discount.isActive(0.05f) == true);   // 1am
        CHECK(discount.isActive(0.5f) == false);   // noon
    }

    SUBCASE("createWeekendSpecial") {
        auto discount = DiscountFactory::createWeekendSpecial("Weekend Deal", 10);
        CHECK(discount.info.name == "Weekend Deal");
        CHECK(discount.isActive(DayOfWeek::Saturday) == true);
        CHECK(discount.isActive(DayOfWeek::Sunday) == true);
        CHECK(discount.isActive(DayOfWeek::Friday) == false);
    }
}

TEST_CASE("Sample tavern menu creation") {
    auto menu = createSampleTavernMenu();

    CHECK(menu.getName() == "The Rusty Tankard Menu");
    CHECK(menu.getEstablishment() == "The Rusty Tankard");

    // Check items exist
    CHECK(menu.getItem("ale_common") != nullptr);
    CHECK(menu.getItem("mead") != nullptr);
    CHECK(menu.getItem("stew") != nullptr);

    // Check discounts exist
    CHECK(menu.getHappyHourDiscounts().size() >= 1);
    CHECK(menu.getDailyDiscounts().size() >= 1);
    CHECK(menu.getWeeklyDiscounts().size() >= 1);

    // Test a pricing scenario: Happy Hour on Thursday (both happy hour and Thirsty Thursday)
    auto priced = menu.calculatePrice("ale_common", 0.7f, DayOfWeek::Thursday, 2, false);
    CHECK(priced.hasDiscount() == true);
    // Should get best of 20% (happy hour) or 10% (thirsty thursday) = 20%
    CHECK(priced.finalPrice == 40);  // 50 - 20% = 40
}

TEST_CASE("PricedItem discountPercentage calculation") {
    PricedItem item;
    item.basePrice = 100;
    item.finalPrice = 80;
    item.totalDiscount = 20;

    CHECK(item.discountPercentage() == doctest::Approx(20.0f));

    item.basePrice = 200;
    item.finalPrice = 150;
    item.totalDiscount = 50;

    CHECK(item.discountPercentage() == doctest::Approx(25.0f));

    // Edge case: zero base price
    item.basePrice = 0;
    CHECK(item.discountPercentage() == doctest::Approx(0.0f));
}
