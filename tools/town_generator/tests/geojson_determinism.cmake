# Determinism check: the same seed must produce byte-identical GeoJSON.
# Catches nondeterminism (e.g. unordered container iteration) in the exporter.
# Usage: cmake -DTOOL=<town_generator> -DWORK_DIR=<dir> -P geojson_determinism.cmake

file(MAKE_DIRECTORY ${WORK_DIR})

execute_process(
    COMMAND ${TOOL} --seed 42 --cells 15 --no-coast
            --geojson ${WORK_DIR}/town_a.geojson ${WORK_DIR}/town_a.svg
    RESULT_VARIABLE RESULT_A
)
if(NOT RESULT_A EQUAL 0)
    message(FATAL_ERROR "First town_generator run failed (${RESULT_A})")
endif()

execute_process(
    COMMAND ${TOOL} --seed 42 --cells 15 --no-coast
            --geojson ${WORK_DIR}/town_b.geojson ${WORK_DIR}/town_b.svg
    RESULT_VARIABLE RESULT_B
)
if(NOT RESULT_B EQUAL 0)
    message(FATAL_ERROR "Second town_generator run failed (${RESULT_B})")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK_DIR}/town_a.geojson ${WORK_DIR}/town_b.geojson
    RESULT_VARIABLE COMPARE_RESULT
)
if(NOT COMPARE_RESULT EQUAL 0)
    message(FATAL_ERROR "GeoJSON output differs between identical-seed runs - exporter is nondeterministic")
endif()

message(STATUS "GeoJSON determinism check passed")
