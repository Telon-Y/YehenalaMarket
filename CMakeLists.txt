cmake_minimum_required(VERSION 3.16)
project(yehenala_sim VERSION 2.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

function(yehenala_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(MINGW)
            target_compile_options(${target} PRIVATE
                -finput-charset=UTF-8 -fexec-charset=UTF-8)
        endif()
    endif()
endfunction()

set(YEHENALA_CORE_SOURCES
    warehouse.cpp
    warehouse_registry.cpp
    warehouse_setup.cpp
    warehouse_order_planning.cpp
    warehouse_logistics.cpp
    warehouse_production.cpp
    warehouse_production_forecast.cpp
    warehouse_audit.cpp
    map_model.cpp
    map_layout.cpp
    map_province_geometry.cpp
    world.cpp
    world_registry.cpp
    world_snapshots.cpp
    world_simulation.cpp
    world_data.cpp
    world_geography.cpp
    world_basemap.cpp
    country_palette.cpp
    province.cpp
    country.cpp
    national_market.cpp
    local_market.cpp
    local_market_setup.cpp
    local_market_commands.cpp
    local_market_step.cpp
    local_market_labor.cpp
    local_market_supply.cpp
    local_market_consumption.cpp
    local_market_finance.cpp
    local_market_market.cpp
    local_market_history.cpp
    price_engine.cpp
    building_template.cpp
    building_manager.cpp
    building_manager_construction.cpp
    building_manager_ai.cpp
    building_manager_employment.cpp
    building_manager_decay.cpp
    building_manager_ownership.cpp
)

set(YEHENALA_MAP_RESOURCE
    "${CMAKE_SOURCE_DIR}/data/ne_50m_admin_0_countries.geojson")
set(YEHENALA_EMBEDDED_MAP_CPP
    "${CMAKE_CURRENT_BINARY_DIR}/world_basemap_embedded.cpp")
add_custom_command(
    OUTPUT "${YEHENALA_EMBEDDED_MAP_CPP}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${YEHENALA_MAP_RESOURCE}
            -DOUTPUT=${YEHENALA_EMBEDDED_MAP_CPP}
            -P "${CMAKE_SOURCE_DIR}/tools/embed_geojson.cmake"
    DEPENDS
        "${YEHENALA_MAP_RESOURCE}"
        "${CMAKE_SOURCE_DIR}/tools/embed_geojson.cmake"
    VERBATIM
)
list(APPEND YEHENALA_CORE_SOURCES "${YEHENALA_EMBEDDED_MAP_CPP}")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE YEHENALA_UI_TEXT_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/*.cpp"
    "${CMAKE_SOURCE_DIR}/*.h")
list(FILTER YEHENALA_UI_TEXT_INPUTS EXCLUDE REGEX "/tests/")
list(FILTER YEHENALA_UI_TEXT_INPUTS EXCLUDE REGEX "/out/")
list(FILTER YEHENALA_UI_TEXT_INPUTS EXCLUDE REGEX "/build/")
set(YEHENALA_UI_TEXT_CATALOG_CPP
    "${CMAKE_CURRENT_BINARY_DIR}/ui_text_catalog_generated.cpp")
add_custom_command(
    OUTPUT "${YEHENALA_UI_TEXT_CATALOG_CPP}"
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_SOURCE_DIR}/tools/generate_ui_text_catalog.py"
            --source-root "${CMAKE_SOURCE_DIR}"
            --output "${YEHENALA_UI_TEXT_CATALOG_CPP}"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/tools/generate_ui_text_catalog.py"
        ${YEHENALA_UI_TEXT_INPUTS}
    VERBATIM
)
set(YEHENALA_REVIEWED_GEOMETRY_SCRIPT
    "${CMAKE_SOURCE_DIR}/tools/build_reviewed_geometry.py")
set(YEHENALA_REVIEWED_GEOMETRY_ADMIN1
    "${CMAKE_SOURCE_DIR}/data/ne_10m_admin_1_states_provinces.geojson")
set(YEHENALA_REVIEWED_GEOMETRY_WATER
    "${CMAKE_SOURCE_DIR}/data/ne_10m_review_water.geojson")
set(YEHENALA_REVIEWED_MAP_RESOURCE
    "${CMAKE_SOURCE_DIR}/data/reviewed_province_geometry.geojson")
set(YEHENALA_REVIEWED_GEOMETRY_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/reviewed_geometry.stamp")
add_custom_command(
    OUTPUT "${YEHENALA_REVIEWED_GEOMETRY_STAMP}"
    COMMAND ${Python3_EXECUTABLE} "${YEHENALA_REVIEWED_GEOMETRY_SCRIPT}"
            --admin1 "${YEHENALA_REVIEWED_GEOMETRY_ADMIN1}"
            --water "${YEHENALA_REVIEWED_GEOMETRY_WATER}"
            --reviewed "${YEHENALA_REVIEWED_MAP_RESOURCE}"
    COMMAND ${CMAKE_COMMAND} -E touch "${YEHENALA_REVIEWED_GEOMETRY_STAMP}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DEPENDS
        "${YEHENALA_REVIEWED_GEOMETRY_SCRIPT}"
        "${YEHENALA_REVIEWED_GEOMETRY_ADMIN1}"
        "${YEHENALA_REVIEWED_GEOMETRY_WATER}"
        "${YEHENALA_REVIEWED_MAP_RESOURCE}"
    VERBATIM
)
add_custom_target(reviewed_geometry
    DEPENDS "${YEHENALA_REVIEWED_GEOMETRY_STAMP}")
set(YEHENALA_EMBEDDED_REVIEWED_MAP_CPP
    "${CMAKE_CURRENT_BINARY_DIR}/reviewed_province_geometry_embedded.cpp")
add_custom_command(
    OUTPUT "${YEHENALA_EMBEDDED_REVIEWED_MAP_CPP}"
    COMMAND ${CMAKE_COMMAND}
            -DINPUT=${YEHENALA_REVIEWED_MAP_RESOURCE}
            -DOUTPUT=${YEHENALA_EMBEDDED_REVIEWED_MAP_CPP}
            -DFUNCTION=EmbeddedReviewedProvinceGeoJson
            -P "${CMAKE_SOURCE_DIR}/tools/embed_geojson.cmake"
    DEPENDS
        "${YEHENALA_REVIEWED_MAP_RESOURCE}"
        "${YEHENALA_REVIEWED_GEOMETRY_SCRIPT}"
        "${YEHENALA_REVIEWED_GEOMETRY_ADMIN1}"
        "${YEHENALA_REVIEWED_GEOMETRY_WATER}"
        "${CMAKE_SOURCE_DIR}/tools/embed_geojson.cmake"
    VERBATIM
)
list(APPEND YEHENALA_CORE_SOURCES "${YEHENALA_EMBEDDED_REVIEWED_MAP_CPP}")

add_library(YehenalaCore STATIC ${YEHENALA_CORE_SOURCES})
# The checked-in GeoJSON is generated input for the embedded map. Ensure the
# reviewed GEOS gate runs before any source embedding or core compilation.
add_dependencies(YehenalaCore reviewed_geometry)
target_include_directories(YehenalaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
yehenala_enable_warnings(YehenalaCore)

# Cross-platform raylib discovery. RAYLIB_ROOT may be supplied explicitly.
if(DEFINED RAYLIB_ROOT AND NOT RAYLIB_ROOT STREQUAL "")
    set(RAYLIB_HINT "${RAYLIB_ROOT}")
else()
    set(RAYLIB_HINT "")
endif()

if(WIN32)
    # Windows uses the static raylib package bundled with the project.
    if(NOT RAYLIB_HINT)
        set(RAYLIB_ROOT "${CMAKE_SOURCE_DIR}/raylib")
    else()
        set(RAYLIB_ROOT "${RAYLIB_HINT}")
    endif()
    message(STATUS "Windows build, using RAYLIB_ROOT=${RAYLIB_ROOT}")
else()
    # Other platforms prefer pkg-config and then search common paths.
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(RAYLIB_PC QUIET IMPORTED_TARGET raylib)
    endif()

    if(RAYLIB_PC_FOUND)
        message(STATUS "Found raylib via pkg-config: ${RAYLIB_PC_LIBRARIES}")
    else()
        find_path(RAYLIB_INCLUDE_DIR raylib.h
            PATHS
                ${RAYLIB_HINT}
                ${RAYLIB_HINT}/include
                /usr/local/include
                /usr/include
        )
        find_library(RAYLIB_LIBRARY raylib
            PATHS
                ${RAYLIB_HINT}
                ${RAYLIB_HINT}/lib
                /usr/local/lib
                /usr/lib
        )
        if(RAYLIB_INCLUDE_DIR AND RAYLIB_LIBRARY)
            set(RAYLIB_PC_FOUND TRUE)
            set(RAYLIB_PC_INCLUDE_DIRS ${RAYLIB_INCLUDE_DIR})
            set(RAYLIB_PC_LIBRARIES ${RAYLIB_LIBRARY})
            message(STATUS
                "Found raylib manually: include=${RAYLIB_INCLUDE_DIR}, lib=${RAYLIB_LIBRARY}")
        else()
            message(WARNING
                "raylib not found. Set RAYLIB_ROOT or install raylib.")
        endif()
    endif()
endif()

add_executable(YehenalaMarket
    ${YEHENALA_UI_TEXT_CATALOG_CPP}
    main.cpp
    debug_report.cpp
    debug_ui.cpp
    debug_ui_shared.cpp
    debug_ui_goods.cpp
    debug_ui_buildings.cpp
    debug_ui_construction.cpp
    debug_ui_macro.cpp
    font_codepoints.cpp
    ui_helpers.cpp
    ui_layout.cpp
    ui_input.cpp
    ui_navigation.cpp
    ui_draw.cpp
    ui_map.cpp
    ui_map_geometry.cpp
    ui_country.cpp
    ui_country_flags.cpp
    ui_construction.cpp
    ui_transport.cpp
)
target_link_libraries(YehenalaMarket PRIVATE YehenalaCore)
yehenala_enable_warnings(YehenalaMarket)

target_include_directories(YehenalaMarket PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_custom_command(
    TARGET YehenalaMarket
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/MingChinese.ttf"
        "$<TARGET_FILE_DIR:YehenalaMarket>/MingChinese.ttf"
)

if(WIN32)
    target_compile_definitions(YehenalaMarket PRIVATE UNICODE _UNICODE)
    target_include_directories(YehenalaMarket PRIVATE "${RAYLIB_ROOT}/include")
    target_link_libraries(YehenalaMarket PRIVATE
        "${RAYLIB_ROOT}/lib/libraylib.a"
        opengl32
        gdi32
        winmm
    )
    if(MINGW)
        target_link_options(YehenalaMarket PRIVATE -static)
    endif()
elseif(RAYLIB_PC_FOUND)
    target_include_directories(YehenalaMarket PRIVATE ${RAYLIB_PC_INCLUDE_DIRS})
    if(TARGET PkgConfig::RAYLIB_PC)
        target_link_libraries(YehenalaMarket PRIVATE PkgConfig::RAYLIB_PC)
    else()
        target_link_directories(YehenalaMarket PRIVATE ${RAYLIB_PC_LIBRARY_DIRS})
        target_link_libraries(YehenalaMarket PRIVATE ${RAYLIB_PC_LIBRARIES})
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(X11 REQUIRED)
        target_link_libraries(YehenalaMarket PRIVATE
            m pthread dl rt ${X11_LIBRARIES})
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        target_link_libraries(YehenalaMarket PRIVATE
            "-framework Cocoa"
            "-framework IOKit"
            "-framework CoreVideo"
            "-framework OpenGL")
    endif()
else()
    message(FATAL_ERROR
        "Cannot build: raylib not found. Install raylib or set RAYLIB_ROOT.")
endif()

set_target_properties(YehenalaMarket PROPERTIES WIN32_EXECUTABLE FALSE)

install(TARGETS YehenalaMarket
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/MingChinese.ttf"
    DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
    add_executable(YehenalaLogisticsTests
        tests/logistics_contract_tests.cpp)
    target_link_libraries(YehenalaLogisticsTests PRIVATE YehenalaCore)
    yehenala_enable_warnings(YehenalaLogisticsTests)
    add_test(NAME logistics_contract COMMAND YehenalaLogisticsTests)

    add_executable(YehenalaSimulationTests
        tests/simulation_tests.cpp)
    target_link_libraries(YehenalaSimulationTests PRIVATE YehenalaCore)
    yehenala_enable_warnings(YehenalaSimulationTests)
    add_test(NAME simulation_end_to_end COMMAND YehenalaSimulationTests)

    add_executable(YehenalaUiTextTests
        tests/ui_text_catalog_tests.cpp
        ${YEHENALA_UI_TEXT_CATALOG_CPP}
        font_codepoints.cpp)
    target_include_directories(YehenalaUiTextTests PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_definitions(YehenalaUiTextTests PRIVATE
        YEHENALA_UI_FONT_PATH="${CMAKE_SOURCE_DIR}/MingChinese.ttf")
    yehenala_enable_warnings(YehenalaUiTextTests)
    add_test(NAME ui_text_catalog COMMAND YehenalaUiTextTests)
endif()
