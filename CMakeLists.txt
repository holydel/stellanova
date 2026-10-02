cmake_minimum_required(VERSION 3.28)

project(stellanova VERSION 0.1.0 LANGUAGES C CXX)

# pith, the engine, checked out beside this repository.
set(SN_PITH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pith" CACHE PATH "pith's checkout")
get_filename_component(SN_PITH_DIR "${SN_PITH_DIR}" ABSOLUTE)
option(SN_BUILD_TESTS "Build Stella Nova's tests" ON)

# pith's build rules and helpers, included here first so that pith's own
# directory and ours share them (cmake/ph_build.cmake in pith).
include(${SN_PITH_DIR}/cmake/ph_build.cmake)
include(${SN_PITH_DIR}/cmake/ph_modules.cmake)
include(${SN_PITH_DIR}/cmake/ph_app.cmake)
include(${SN_PITH_DIR}/cmake/ph_shaders.cmake)
include(${SN_PITH_DIR}/cmake/ph_content.cmake)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(SN_BUILD_TESTS)
	enable_testing()
endif()

# pith without its samples, tests and tools: shaders and packs are made by the
# host's pith tool (PH_HOST_PITH), which scripts/build.ps1 builds first.
add_subdirectory(${SN_PITH_DIR} ${CMAKE_BINARY_DIR}/pith)

# The game's content sources (content/README.md).
ph_fetch_content(${CMAKE_CURRENT_SOURCE_DIR}/content)

add_subdirectory(sim)
add_subdirectory(bots)
add_subdirectory(server)
add_subdirectory(client)
if(SN_BUILD_TESTS)
	add_subdirectory(tests)
endif()
