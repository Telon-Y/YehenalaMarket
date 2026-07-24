cmake_minimum_required(VERSION 3.16)
project(yehenala_sim VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# === raylib 路径（请确保预编译包位于此相对路径） ===
set(RAYLIB_ROOT "${CMAKE_SOURCE_DIR}/raylib")

# 定义可执行文件
add_executable(sim
    main.cpp
    market_simulation.cpp
    building_template.cpp
    ui.cpp
)

# 链接 raylib 静态库及系统依赖
target_link_libraries(sim PRIVATE
    "${RAYLIB_ROOT}/lib/libraylib.a"
    opengl32
    gdi32
    winmm
)

# 头文件路径
target_include_directories(sim PRIVATE "${RAYLIB_ROOT}/include")

# 稳定优化（避免 LTO 和 native 指令集带来的链接问题）
target_compile_options(sim PRIVATE -O2)

# === 关键：保留控制台窗口，便于查看日志 ===
set_target_properties(sim PROPERTIES
    WIN32_EXECUTABLE FALSE
)

if(WIN32)
    target_compile_definitions(sim PRIVATE UNICODE _UNICODE)
endif()