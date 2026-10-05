# Generic file variable names so this template is reusable for other libraries.
# Place your public headers in PublicHeader/ and private sources/headers in Private/
file(GLOB_RECURSE HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

file(GLOB_RECURSE MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" # top-level cpp if any
)
add_executable(Tests ${HEADERS} ${MODULE_SOURCES})
target_link_libraries(Tests PRIVATE Engine Slate Application Core RHIVulkan Renderer RHIOpenGL RHIDirectX12)
target_include_directories(Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})