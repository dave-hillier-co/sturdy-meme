#include <doctest/doctest.h>
#include "town_generator2/geom/Point.hpp"
#include "town_generator2/geom/Polygon.hpp"
#include "town_generator2/building/Model.hpp"
#include "town_generator2/building/Patch.hpp"
#include "town_generator2/building/Cutter.hpp"
#include "town_generator2/wards/Ward.hpp"
#include "town_generator2/wards/AllWards.hpp"
#include "town_generator2/utils/Random.hpp"

using namespace town_generator2;
using namespace town_generator2::geom;
using namespace town_generator2::building;
using namespace town_generator2::wards;

TEST_SUITE("Ward createAlleys") {
    TEST_CASE("createAlleys subdivides polygon") {
        utils::Random::reset(42);

        Polygon block = Polygon::rect(20, 20);
        block.offset(Point(10, 10)); // Move to positive quadrant

        auto buildings = Ward::createAlleys(
            block,
            50.0,   // minSq
            0.5,    // gridChaos
            0.5,    // sizeChaos
            0.04    // emptyProb
        );

        CHECK(buildings.size() >= 1);

        // All buildings should have positive area
        for (const auto& b : buildings) {
            CHECK(std::abs(b.square()) > 0);
        }
    }

    TEST_CASE("createAlleys with high gridChaos produces varied results") {
        utils::Random::reset(123);

        Polygon block = Polygon::rect(30, 30);
        block.offset(Point(15, 15));

        auto buildings = Ward::createAlleys(block, 40.0, 0.9, 0.8, 0.0);

        // Should produce multiple buildings
        CHECK(buildings.size() >= 2);
    }

    TEST_CASE("createAlleys with small minSq produces more buildings") {
        utils::Random::reset(456);

        Polygon block = Polygon::rect(20, 20);
        block.offset(Point(10, 10));

        auto fewBuildings = Ward::createAlleys(block, 100.0, 0.5, 0.5, 0.0);
        auto manyBuildings = Ward::createAlleys(block, 20.0, 0.5, 0.5, 0.0);

        // Smaller minSq should produce more subdivisions
        CHECK(manyBuildings.size() >= fewBuildings.size());
    }

    TEST_CASE("createAlleys emptyProb affects output count") {
        utils::Random::reset(789);

        Polygon block = Polygon::rect(20, 20);
        block.offset(Point(10, 10));

        // Run multiple times to get statistical result
        int fullCount = 0, emptyCount = 0;
        for (int trial = 0; trial < 10; ++trial) {
            utils::Random::reset(trial);
            auto full = Ward::createAlleys(block, 30.0, 0.5, 0.5, 0.0);
            fullCount += static_cast<int>(full.size());

            utils::Random::reset(trial);
            auto withEmpty = Ward::createAlleys(block, 30.0, 0.5, 0.5, 0.5);
            emptyCount += static_cast<int>(withEmpty.size());
        }

        // Higher emptyProb should produce fewer buildings on average
        CHECK(emptyCount < fullCount);
    }
}

TEST_SUITE("Ward createOrthoBuilding") {
    TEST_CASE("createOrthoBuilding creates building footprints") {
        utils::Random::reset(42);

        Polygon block = Polygon::rect(20, 20);
        block.offset(Point(10, 10));

        auto buildings = Ward::createOrthoBuilding(block, 50.0, 0.8);

        CHECK(buildings.size() >= 1);
    }

    TEST_CASE("createOrthoBuilding small block returns original") {
        utils::Random::reset(42);

        Polygon smallBlock = Polygon::rect(5, 5);
        smallBlock.offset(Point(2.5, 2.5));

        auto buildings = Ward::createOrthoBuilding(smallBlock, 100.0, 0.8);

        CHECK(buildings.size() == 1);
    }

    TEST_CASE("createOrthoBuilding fill affects density") {
        utils::Random::reset(42);

        Polygon block = Polygon::rect(30, 30);
        block.offset(Point(15, 15));

        int lowFillTotal = 0, highFillTotal = 0;
        for (int trial = 0; trial < 5; ++trial) {
            utils::Random::reset(trial * 100);
            auto lowFill = Ward::createOrthoBuilding(block, 40.0, 0.3);
            lowFillTotal += static_cast<int>(lowFill.size());

            utils::Random::reset(trial * 100);
            auto highFill = Ward::createOrthoBuilding(block, 40.0, 0.9);
            highFillTotal += static_cast<int>(highFill.size());
        }

        // Higher fill should produce more buildings on average
        CHECK(highFillTotal >= lowFillTotal);
    }
}

TEST_SUITE("Cutter semiRadial") {
    TEST_CASE("semiRadial creates sectors") {
        Polygon hex = Polygon::regular(6, 10.0);

        auto sectors = Cutter::semiRadial(hex, nullptr, 0.5);

        CHECK(sectors.size() >= 1);
    }

    TEST_CASE("semiRadial with specified center") {
        Polygon square({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        auto center = square.ptr(0);

        auto sectors = Cutter::semiRadial(square, center, 0.0);

        // Should create sectors radiating from the specified corner
        CHECK(sectors.size() >= 1);
    }
}

TEST_SUITE("Ward types labels") {
    TEST_CASE("CraftsmenWard label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        CraftsmenWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Craftsmen");
    }

    TEST_CASE("MerchantWard label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        MerchantWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Merchant");
    }

    TEST_CASE("Slum label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        Slum ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Slum");
    }

    TEST_CASE("Park label") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        Park ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Park");
    }

    TEST_CASE("Market label") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        Market ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Market");
    }

    TEST_CASE("Cathedral label") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        Cathedral ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Temple");
    }

    TEST_CASE("GateWard label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        GateWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Gate");
    }

    TEST_CASE("MilitaryWard label") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        MilitaryWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Military");
    }

    TEST_CASE("Farm label") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        Farm ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Farm");
    }

    TEST_CASE("AdministrationWard label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        AdministrationWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Administration");
    }

    TEST_CASE("PatriciateWard label") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        PatriciateWard ward(nullptr, &patch);

        CHECK(ward.getLabel() == "Patriciate");
    }
}

TEST_SUITE("Ward constants") {
    TEST_CASE("Street width constants are positive") {
        CHECK(Ward::MAIN_STREET > 0);
        CHECK(Ward::REGULAR_STREET > 0);
        CHECK(Ward::ALLEY > 0);
    }

    TEST_CASE("Street width ordering") {
        CHECK(Ward::MAIN_STREET > Ward::REGULAR_STREET);
        CHECK(Ward::REGULAR_STREET > Ward::ALLEY);
    }
}

TEST_SUITE("Ward geometry mutation") {
    TEST_CASE("createGeometry produces independent polygons") {
        utils::Random::reset(42);

        Polygon shape({Point(0, 0), Point(20, 0), Point(20, 20), Point(0, 20)});
        Patch patch(shape);
        patch.withinCity = true;
        patch.withinWalls = true;

        // Create a minimal mock model would be needed for full test
        // For now, test that the geometry vector can be populated
        CraftsmenWard ward(nullptr, &patch);

        // Directly test createAlleys instead
        Polygon block = Polygon::rect(15, 15);
        block.offset(Point(7.5, 7.5));

        auto buildings = Ward::createAlleys(block, 20.0, 0.5, 0.5, 0.04);

        // Mutate one building
        if (!buildings.empty() && buildings[0].length() > 0) {
            buildings[0][0].x = 1000;

            // Other buildings should not be affected
            bool otherAffected = false;
            for (size_t i = 1; i < buildings.size(); ++i) {
                for (size_t j = 0; j < buildings[i].length(); ++j) {
                    if (buildings[i][j].x == 1000) {
                        otherAffected = true;
                    }
                }
            }
            CHECK_FALSE(otherAffected);
        }
    }
}

TEST_SUITE("Cutter ring") {
    TEST_CASE("ring creates peeled layers") {
        Polygon square({Point(0, 0), Point(20, 0), Point(20, 20), Point(0, 20)});

        auto layers = Cutter::ring(square, 2.0);

        CHECK(layers.size() >= 1);
    }

    TEST_CASE("ring with different thickness") {
        Polygon square({Point(0, 0), Point(30, 0), Point(30, 30), Point(0, 30)});

        auto thinLayers = Cutter::ring(square, 1.0);
        auto thickLayers = Cutter::ring(square, 5.0);

        // Both should produce some layers
        CHECK(thinLayers.size() >= 1);
        CHECK(thickLayers.size() >= 1);
    }

    TEST_CASE("ring on hexagon") {
        Polygon hex = Polygon::regular(6, 15.0);

        auto layers = Cutter::ring(hex, 2.0);

        CHECK(layers.size() >= 1);
    }
}

TEST_SUITE("Patch basic operations") {
    TEST_CASE("Patch construction from shape") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        CHECK(patch.shape.length() == 4);
        CHECK(patch.withinCity == false);
        CHECK(patch.withinWalls == false);
        CHECK(patch.ward == nullptr);
    }

    TEST_CASE("Patch shape mutation propagates") {
        auto p1 = makePoint(0, 0);
        auto p2 = makePoint(10, 0);
        auto p3 = makePoint(10, 10);
        auto p4 = makePoint(0, 10);

        Polygon shape({p1, p2, p3, p4});
        Patch patch(shape);

        // Mutate original point
        p1->x = 5;

        // Patch shape should see the change
        CHECK(patch.shape[0].x == 5);
    }

    TEST_CASE("Patch state flags") {
        Polygon shape({Point(0, 0), Point(10, 0), Point(10, 10), Point(0, 10)});
        Patch patch(shape);

        patch.withinCity = true;
        patch.withinWalls = true;

        CHECK(patch.withinCity == true);
        CHECK(patch.withinWalls == true);
    }
}
