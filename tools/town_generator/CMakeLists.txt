cmake_minimum_required(VERSION 3.16)
project(town_generator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header files
set(HEADERS
    include/town_generator/geom/Point.h
    include/town_generator/geom/Segment.h
    include/town_generator/geom/GeomUtils.h
    include/town_generator/geom/Circle.h
    include/town_generator/geom/Polygon.h
    include/town_generator/geom/Graph.h
    include/town_generator/geom/Spline.h
    include/town_generator/geom/Voronoi.h
    include/town_generator/utils/MathUtils.h
    include/town_generator/utils/Random.h
    include/town_generator/building/Patch.h
    include/town_generator/building/Cutter.h
    include/town_generator/building/Topology.h
    include/town_generator/building/CurtainWall.h
    include/town_generator/building/Model.h
    include/town_generator/wards/Ward.h
    include/town_generator/wards/Castle.h
    include/town_generator/wards/Cathedral.h
    include/town_generator/wards/Market.h
    include/town_generator/wards/CraftsmenWard.h
    include/town_generator/wards/MerchantWard.h
    include/town_generator/wards/PatriciateWard.h
    include/town_generator/wards/CommonWard.h
    include/town_generator/wards/AdministrationWard.h
    include/town_generator/wards/MilitaryWard.h
    include/town_generator/wards/GateWard.h
    include/town_generator/wards/Slum.h
    include/town_generator/wards/Farm.h
    include/town_generator/wards/Park.h
    include/town_generator/rendering/Palette.h
    include/town_generator/rendering/Brush.h
    include/town_generator/rendering/CityMap.h
    include/town_generator/rendering/SVGWriter.h
)

# Source files
set(SOURCES
    src/main.cpp
    src/geom/Point.cpp
    src/geom/Segment.cpp
    src/geom/GeomUtils.cpp
    src/geom/Circle.cpp
    src/geom/Polygon.cpp
    src/geom/Graph.cpp
    src/geom/Spline.cpp
    src/geom/Voronoi.cpp
    src/utils/MathUtils.cpp
    src/utils/Random.cpp
    src/building/Patch.cpp
    src/building/Cutter.cpp
    src/building/Topology.cpp
    src/building/CurtainWall.cpp
    src/building/Model.cpp
    src/wards/Ward.cpp
    src/wards/Castle.cpp
    src/wards/Cathedral.cpp
    src/wards/Market.cpp
    src/wards/CraftsmenWard.cpp
    src/wards/MerchantWard.cpp
    src/wards/PatriciateWard.cpp
    src/wards/CommonWard.cpp
    src/wards/AdministrationWard.cpp
    src/wards/MilitaryWard.cpp
    src/wards/GateWard.cpp
    src/wards/Slum.cpp
    src/wards/Farm.cpp
    src/wards/Park.cpp
    src/rendering/Palette.cpp
    src/rendering/Brush.cpp
    src/rendering/CityMap.cpp
    src/rendering/SVGWriter.cpp
)

add_executable(town_generator ${SOURCES} ${HEADERS})

target_include_directories(town_generator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Install target
install(TARGETS town_generator DESTINATION bin)
