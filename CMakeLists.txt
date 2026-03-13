cmake_minimum_required(VERSION 3.16)
project(sinistar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED)

add_executable(sinistar src/main.cpp src/stb_impl.cpp)
target_include_directories(sinistar PRIVATE src)

# Link SDL2 - handle both modern (imported target) and legacy (variables) style
if(TARGET SDL2::SDL2)
    target_link_libraries(sinistar PRIVATE SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
        target_link_libraries(sinistar PRIVATE SDL2::SDL2main)
    endif()
else()
    target_include_directories(sinistar PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sinistar PRIVATE ${SDL2_LIBRARIES})
endif()

if(UNIX)
    target_link_libraries(sinistar PRIVATE m)
endif()

# Compiler warnings
if(MSVC)
    target_compile_definitions(sinistar PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(sinistar PRIVATE /W3 /wd4244 /wd4267)
else()
    target_compile_options(sinistar PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# Place executable in project root so it finds assets/ directory
set_target_properties(sinistar PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)

# Asset extraction tool (no SDL2 dependency, run once to generate PNG sprite sheets)
add_executable(extract_assets EXCLUDE_FROM_ALL tools/extract_assets.cpp)
target_include_directories(extract_assets PRIVATE src)
if(MSVC)
    target_compile_definitions(extract_assets PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(extract_assets PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)
