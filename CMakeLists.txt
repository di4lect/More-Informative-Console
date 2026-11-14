cmake_minimum_required(VERSION 3.21)

# vcpkg (set VCPKG_ROOT) - static triplet so the DLL has no runtime dependencies
if (DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif ()
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(VampireEyeGlowFix VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(_UNICODE NOMINMAX)

# CommonLibSSE-NG (alandtse CommonLibVR fork): one DLL for SE 1.5.97, AE 1.6.x and VR
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibVR CommonLibSSE EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)

add_library(${PROJECT_NAME} SHARED src/main.cpp)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog)

if (MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /Zi
		"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>")
	target_link_options(${PROJECT_NAME} PRIVATE
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>")
endif ()
