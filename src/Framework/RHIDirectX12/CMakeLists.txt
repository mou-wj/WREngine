set(TARGET_NAME RHIDirectX12)

# Reconfigure existing VS/Ninja and VS Code builds when module files are added
# or removed, so newly introduced implementation files reach the linker.
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader/*.hpp"
)

file(GLOB_RECURSE PRIVATE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.hpp"
)

file(GLOB_RECURSE MODULE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Private/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if(NOT MODULE_SOURCES)
    message(WARNING "No source files found for ${TARGET_NAME} in ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

add_library(${TARGET_NAME} SHARED
    ${MODULE_SOURCES}
    ${PUBLIC_HEADERS}
    ${PRIVATE_HEADERS}
)

target_include_directories(${TARGET_NAME}
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/PublicHeader"
)

target_link_libraries(${TARGET_NAME} PUBLIC RHI RenderCore)

if(WIN32)
    target_link_libraries(${TARGET_NAME} PUBLIC d3d12 dxgi)
endif()

target_compile_definitions(${TARGET_NAME}
    PRIVATE DEBUG_INFO=1
)

_3rds_copy_runtime_deps(${TARGET_NAME} "${BINARY_DIR}")
_define_export_macro(${TARGET_NAME})

set_target_properties(${TARGET_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /W4 /permissive-)
else()
    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
