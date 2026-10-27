

# Target name (change as needed per module)
set(TARGET_NAME SlateCore)

# Generic file variable names so this template is reusable for other libraries.
# Place your public headers in PublicHeader/ and private sources/headers in Private/
file(GLOB_RECURSE PUBLIC_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.hpp"
)

file(GLOB_RECURSE PRIVATE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.hpp"
)

file(GLOB_RECURSE MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" # top-level cpp if any
)

# Warn if no sources found (helps reuse the template)
if(NOT MODULE_SOURCES)
    message(WARNING "No source files found for ${TARGET_NAME} in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Create shared library target
add_library(${TARGET_NAME} SHARED
    ${MODULE_SOURCES}
    ${PUBLIC_HEADERS}
    ${PRIVATE_HEADERS}
 "Private/Window.cpp")

# Expose public headers to consumers
target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader"
)
target_link_libraries(${TARGET_NAME} PUBLIC Core)
if(WIN32)
    target_link_libraries(${TARGET_NAME} PRIVATE shell32)
endif()

# Position independent code for shared libs
set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

string(TOUPPER "${TARGET_NAME}" TARGET_NAME_UPPER)
if (WIN32)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE ${TARGET_NAME_UPPER}_API=__declspec\(dllexport\)
    INTERFACE ${TARGET_NAME_UPPER}_API=__declspec\(dllimport\)
  )
else()
  target_compile_definitions(${TARGET_NAME_UPPER}
    INTERFACE ${TARGET_NAME_UPPER}_API=__attribute__\(\(visibility\(\"default\"\)\)\)
  )
endif()

# Reasonable compile warnings
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
