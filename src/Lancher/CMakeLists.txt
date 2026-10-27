# Generic file variable names so this template is reusable for other libraries.
# Place your public headers in PublicHeader/ and private sources/headers in Private/
file(GLOB_RECURSE HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB_RECURSE MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" # top-level cpp if any
)

add_executable(Langcher ${HEADERS} ${MODULE_SOURCES})
find_package(nlohmann_json REQUIRED)
target_link_libraries(Langcher PRIVATE Engine SlateCore Core nlohmann_json::nlohmann_json)
# Build runtime-selected apps without importing/starting both ApplicationBase owners.
add_dependencies(Langcher Application Editor RHIVulkan Renderer SlateRHIRenderer)
configure_file(apps.json "${BINARY_DIR}/apps.json" COPYONLY)
target_include_directories(Langcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
