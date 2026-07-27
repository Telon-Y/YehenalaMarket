cmake_minimum_required(VERSION 3.16)
project(yehenala_sim VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RAYLIB_ROOT "${CMAKE_SOURCE_DIR}/raylib")

add_executable(sim
    main.cpp
    world.cpp
    local_market.cpp
    building_template.cpp
    building_manager.cpp
    ui.cpp
)

target_link_libraries(sim PRIVATE
    "${RAYLIB_ROOT}/lib/libraylib.a"
    opengl32
    gdi32
    winmm
)

target_include_directories(sim PRIVATE "${RAYLIB_ROOT}/include")
target_compile_options(sim PRIVATE -O2)

set_target_properties(sim PROPERTIES WIN32_EXECUTABLE FALSE)

if(WIN32)
    target_compile_definitions(sim PRIVATE UNICODE _UNICODE)
    target_link_options(sim PRIVATE -static)
endif()