cmake_minimum_required(VERSION 3.21)

project(
	FireBladeStandalone
	VERSION 0.1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# static CRT, matches the vcpkg triplet x64-windows-static
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# one DLL for SE (1.5.97) + AE, no VR
set(ENABLE_SKYRIM_SE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL " " FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL " " FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLib tests in consumer" FORCE)

set(COMMONLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibVR" CACHE PATH "CommonLibSSE NG (alandtse/CommonLibVR, branch ng)")
if(NOT EXISTS "${COMMONLIB_DIR}/CMakeLists.txt")
	message(FATAL_ERROR
		"CommonLib not found in ${COMMONLIB_DIR}.\n"
		"Clone https://github.com/alandtse/CommonLibVR (tag v13.1.0, with submodules) into extern/CommonLibVR.")
endif()

add_subdirectory("${COMMONLIB_DIR}" CommonLibSSE EXCLUDE_FROM_ALL)
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

add_commonlibsse_plugin(${PROJECT_NAME}
	AUTHOR "personal use"
	SOURCES
		src/Main.cpp
		src/Settings.cpp
		src/Core.cpp
		src/Events.cpp
		src/Menu.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /Zc:preprocessor /utf-8 /W3 /wd4100 /wd4189)
endif()

# copy the DLL into the final Data/ layout (settings INI ships with the data pack, not with the code)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/package/SKSE/Plugins"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${CMAKE_BINARY_DIR}/package/SKSE/Plugins/"
)
