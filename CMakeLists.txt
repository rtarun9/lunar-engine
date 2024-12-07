cmake_minimum_required(VERSION 3.20)

project(lunar-engine 
	VERSION 1.0
	DESCRIPTION "Yet another vulkan renderer :)"
	LANGUAGES CXX)

find_package(Vulkan REQUIRED)

add_executable(lunar-engine src/main.cpp)

target_include_directories(lunar-engine PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(lunar-engine PRIVATE ${Vulkan_LIBRARIES})

# Reference for using SDL3 with cmake's FetchContent : https://wiki.libsdl.org/SDL3/README/cmake
include(FetchContent)

FetchContent_Declare(SDL2
	GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
	GIT_TAG release-2.30.10
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
)

FetchContent_Declare(vk_bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
	GIT_TAG        v1.3.302
)

FetchContent_MakeAvailable(SDL2 vk_bootstrap)

target_link_libraries(lunar-engine PRIVATE SDL2::SDL2 vk-bootstrap::vk-bootstrap)
