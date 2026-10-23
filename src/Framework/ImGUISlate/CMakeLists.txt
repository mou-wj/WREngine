

# Target name (change as needed per module)
set(TARGET_NAME ImGuiSlate)

# Generic file variable names so this template is reusable for other libraries.
# Place your public headers in PublicHeader/ and private sources/headers in Private/
file(GLOB_RECURSE PUBLIC_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.hpp"
    "${IMGUI_HEADER_FILES}"
)

file(GLOB_RECURSE PRIVATE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.hpp"
)

file(GLOB_RECURSE MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" # top-level cpp if any
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c" # top-level cpp if any
    
)

# Warn if no sources found (helps reuse the template)
if(NOT MODULE_SOURCES)
    message(WARNING "No source files found for ${TARGET_NAME} in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

list(FILTER IMGUI_SOURCE_FILES EXCLUDE REGEX "[/\\]backends[/\\]imgui_impl_.*\\.cpp$")

# Create shared library target
add_library(${TARGET_NAME} SHARED
    ${MODULE_SOURCES}
    ${PUBLIC_HEADERS}
    ${PRIVATE_HEADERS}
    ${IMGUI_SOURCE_FILES}
)

# Expose public headers to consumers
target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader"
        "${IMGUI_INCLUDE_DIR}"
        "${IMGUI_INCLUDE_DIR}/backends"
)
target_link_libraries(${TARGET_NAME} PUBLIC SlateCore RenderCore)

# Position independent code for shared libs
set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

string(TOUPPER "${TARGET_NAME}" TARGET_NAME_UPPER)
if (WIN32)
  target_compile_definitions(${TARGET_NAME}
    PRIVATE ${TARGET_NAME_UPPER}_API=__declspec\(dllexport\)
    IMGUI_API=__declspec\(dllexport\)
    INTERFACE ${TARGET_NAME_UPPER}_API=__declspec\(dllimport\)
    IMGUI_API=__declspec\(dllimport\)
  )
else()
  target_compile_definitions(${TARGET_NAME_UPPER}
    INTERFACE ${TARGET_NAME_UPPER}_API=__attribute__\(\(visibility\(\"default\"\)\)\)
    IMGUI_API=__attribute__\(\(visibility\(\"default\"\)\)\)
  )
endif()

# Reasonable compile warnings
if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
