cmake_minimum_required(VERSION 3.16)
project(yehenala_sim VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(YEHENALA_CORE_SOURCES
    world.cpp
    local_market.cpp
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

add_library(YehenalaCore STATIC ${YEHENALA_CORE_SOURCES})
target_include_directories(YehenalaCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# ============ 跨平台 raylib 查找 ============
# 允许用户显式指定：-DRAYLIB_ROOT=...
if(DEFINED RAYLIB_ROOT AND NOT RAYLIB_ROOT STREQUAL "")
    set(RAYLIB_HINT "${RAYLIB_ROOT}")
else()
    set(RAYLIB_HINT "")
endif()

if(WIN32)
    # Windows：使用项目自带的 raylib 静态库
    if(NOT RAYLIB_HINT)
        set(RAYLIB_ROOT "${CMAKE_SOURCE_DIR}/raylib")
    else()
        set(RAYLIB_ROOT "${RAYLIB_HINT}")
    endif()
    message(STATUS "Windows build, using RAYLIB_ROOT=${RAYLIB_ROOT}")
else()
    # Linux / macOS / 其他：优先使用 pkg-config 查找 raylib
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(RAYLIB_PC QUIET IMPORTED_TARGET raylib)
    endif()

    if(RAYLIB_PC_FOUND)
        message(STATUS "Found raylib via pkg-config: ${RAYLIB_PC_LIBRARIES}")
    else()
        # pkg-config 未找到时回退到常见路径
        find_path(RAYLIB_INCLUDE_DIR raylib.h
                  PATHS ${RAYLIB_HINT}
                        ${RAYLIB_HINT}/include
                        /usr/local/include
                        /usr/include)
        find_library(RAYLIB_LIBRARY raylib
                     PATHS ${RAYLIB_HINT}
                           ${RAYLIB_HINT}/lib
                           /usr/local/lib
                           /usr/lib)
        if(RAYLIB_INCLUDE_DIR AND RAYLIB_LIBRARY)
            set(RAYLIB_PC_FOUND TRUE)
            set(RAYLIB_PC_INCLUDE_DIRS ${RAYLIB_INCLUDE_DIR})
            set(RAYLIB_PC_LIBRARIES ${RAYLIB_LIBRARY})
            message(STATUS "Found raylib manually: include=${RAYLIB_INCLUDE_DIR}, lib=${RAYLIB_LIBRARY}")
        else()
            message(WARNING "raylib not found via pkg-config or common paths. "
                            "Set -DRAYLIB_ROOT=/path/to/raylib or install raylib.")
        endif()
    endif()
endif()

# ============ 可执行文件 ============
add_executable(YehenalaMarket
    main.cpp
    font_codepoints.cpp
    ui_helpers.cpp
    ui_input.cpp
    ui_draw.cpp
)
target_link_libraries(YehenalaMarket PRIVATE YehenalaCore)

# ============ 链接 raylib ============
if(WIN32)
    target_link_libraries(YehenalaMarket PRIVATE
        "${RAYLIB_ROOT}/lib/libraylib.a"
        opengl32
        gdi32
        winmm
    )
    target_include_directories(YehenalaMarket PRIVATE "${RAYLIB_ROOT}/include")
    target_compile_definitions(YehenalaMarket PRIVATE UNICODE _UNICODE)
    target_link_options(YehenalaMarket PRIVATE -static)
else()
    if(RAYLIB_PC_FOUND)
        target_include_directories(YehenalaMarket PRIVATE ${RAYLIB_PC_INCLUDE_DIRS})
        if(TARGET PkgConfig::RAYLIB_PC)
            target_link_libraries(YehenalaMarket PRIVATE PkgConfig::RAYLIB_PC)
        else()
            target_link_directories(YehenalaMarket PRIVATE ${RAYLIB_PC_LIBRARY_DIRS})
            target_link_libraries(YehenalaMarket PRIVATE ${RAYLIB_PC_LIBRARIES})
        endif()
        # 链接附加依赖（Linux 通常需要这些；macOS 可能需要额外的 framework）
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(X11 REQUIRED)
            target_link_libraries(YehenalaMarket PRIVATE
                m pthread dl rt
                ${X11_LIBRARIES}
            )
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            # macOS 需要链接相应 framework
            target_link_libraries(YehenalaMarket PRIVATE
                "-framework Cocoa"
                "-framework IOKit"
                "-framework CoreVideo"
                "-framework OpenGL"
            )
        endif()
    else()
        message(FATAL_ERROR "Cannot build: raylib not found. Install raylib or set RAYLIB_ROOT.")
    endif()
endif()

# ============ 编译选项 ============
if(MSVC)
    target_compile_options(YehenalaCore PRIVATE /O2 /W4 /utf-8)
    target_compile_options(YehenalaMarket PRIVATE /O2 /W4 /utf-8)
else()
    target_compile_options(YehenalaCore PRIVATE -O2 -Wall -Wextra -Wpedantic)
    target_compile_options(YehenalaMarket PRIVATE -O2 -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_compile_options(YehenalaCore PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
        target_compile_options(YehenalaMarket PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
    endif()
endif()

set_target_properties(YehenalaMarket PROPERTIES WIN32_EXECUTABLE FALSE)

add_executable(YehenalaProbe tools/simulation_probe.cpp)
target_link_libraries(YehenalaProbe PRIVATE YehenalaCore)

add_executable(FontCodepointProbe tools/font_codepoint_probe.cpp font_codepoints.cpp)
target_include_directories(FontCodepointProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(BUILD_TESTING)
    add_executable(YehenalaTests tests/simulation_tests.cpp)
    target_link_libraries(YehenalaTests PRIVATE YehenalaCore)
    if(MSVC)
        target_compile_options(YehenalaTests PRIVATE /W4 /utf-8)
    else()
        target_compile_options(YehenalaTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME simulation_invariants COMMAND YehenalaTests)
    add_test(NAME simulation_52_weeks COMMAND YehenalaProbe 52 52)
    add_test(NAME simulation_700_weeks COMMAND YehenalaProbe 700 700)
    add_test(NAME simulation_2600_weeks COMMAND YehenalaProbe 2600 2600)
    add_test(NAME simulation_6000_weeks COMMAND YehenalaProbe 6000 6000)
    add_test(NAME font_all_mapped_codepoints
             COMMAND FontCodepointProbe "${CMAKE_CURRENT_SOURCE_DIR}/MingChinese.ttf")
endif()
