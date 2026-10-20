if(NOT WIN32)
	#current surpported platform is windows
	return()
endif()


cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 20)
set(TargetName RenderingLearn)
project(${TargetName})
include(${CMAKE_SOURCE_DIR}/cmake/Find3rdsGenerated.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Functions.cmake)
set(BINARY_DIR "${CMAKE_BINARY_DIR}")
message("out: ${BINARY_DIR}")
# Module loading and runtime DLL copies use BINARY_DIR. Keep generated targets
# there too: a generator expression prevents multi-config generators from
# appending an extra Debug/Release directory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$<1:${BINARY_DIR}>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "$<1:${BINARY_DIR}>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "$<1:${BINARY_DIR}>")
message("INSTALLDIR: ${INSTALL_DIR}/${CONFIG}")
add_compile_definitions(
#DEBUG_INFO=1
NOMINMAX
)

add_subdirectory(src)
