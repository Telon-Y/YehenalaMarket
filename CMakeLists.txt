cmake_minimum_required(VERSION 3.16)
project(yehenala_sim VERSION 1.1 LANGUAGES CXX)

# 使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加可执行文件，并列出所有 .cpp 源文件
add_executable(sim
    main.cpp
    market_simulation.cpp
    building_template.cpp
    csv_writer.cpp
)

# 若需要优化，可在 CMake 调用时指定 -DCMAKE_BUILD_TYPE=Release
# 这里默认 Release 模式并添加优化标志
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        target_compile_options(sim PRIVATE /O2 /arch:AVX2)
    else()
        target_compile_options(sim PRIVATE -O3 -march=native -flto)
    endif()
endif()

# Windows 下 Unicode 输出支持（可选，但无害）
if(WIN32)
    target_compile_definitions(sim PRIVATE UNICODE _UNICODE)
endif()