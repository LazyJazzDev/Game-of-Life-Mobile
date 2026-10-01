cmake_minimum_required(VERSION 3.25)
project(GameOfLifeHarmony LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
  message(FATAL_ERROR "Build the native library through DevEco (build_hap.sh)")
endif()
set(LONGMARCH_HEADERS "$ENV{LONGMARCH_HEADERS}" CACHE PATH "Portable dependency headers")
get_filename_component(GOL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LONGMARCH_ROOT "${GOL_ROOT}/LongMarch")
set(ROOT "${LONGMARCH_ROOT}")
include("${GOL_ROOT}/mobile/Dependencies.cmake")
if(NOT LONGMARCH_HEADERS)
  set(LONGMARCH_HEADERS "${LONGMARCH_MOBILE_HEADERS}")
endif()
if(NOT EXISTS "${LONGMARCH_HEADERS}/glm/glm.hpp" OR NOT EXISTS "${LONGMARCH_HEADERS}/vk_mem_alloc.h")
  message(FATAL_ERROR "Set LONGMARCH_HEADERS to portable headers, including VulkanMemoryAllocator; see README.md")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Engine: grassland utilities, graphics and the Vulkan backend, and the Life rules.
file(GLOB UTIL "${ROOT}/code/grassland/util/*.cpp")
file(GLOB GRAPHICS "${ROOT}/code/grassland/graphics/*.cpp")
file(GLOB_RECURSE VULKAN "${ROOT}/code/grassland/graphics/backend/vulkan/helper/*.cpp")
file(GLOB BACKEND "${ROOT}/code/grassland/graphics/backend/vulkan/*.cpp")
list(FILTER BACKEND EXCLUDE REGEX "/vulkan_(window|imgui_assets)\\.cpp$")
add_library(gol_engine STATIC ${UTIL} ${GRAPHICS} ${VULKAN} ${BACKEND}
  "${ROOT}/code/grassland/math/math_mesh.cxx" "${mikktspace_SOURCE_DIR}/mikktspace.c"
  "${ROOT}/demo/gol/game_of_life_lib/game_of_life_lib.cpp")
target_include_directories(gol_engine PUBLIC "${ROOT}/code" "${ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}"
  "${LONGMARCH_HEADERS}" "${LONGMARCH_HEADERS}/eigen3" "${LONGMARCH_HEADERS}/stb" "${mikktspace_SOURCE_DIR}")
target_compile_definitions(gol_engine PUBLIC LONGMARCH_VULKAN_ENABLED LONGMARCH_HEADLESS
  FMT_HEADER_ONLY NDEBUG LONGMARCH_ASSETS_DIR="")
# The app loads SPIR-V prepared offline; Slang is never linked.
target_compile_definitions(gol_engine PRIVATE LONGMARCH_OFFLINE_SHADERS)
target_link_libraries(gol_engine PUBLIC vulkan)

# Slang sources are embedded only to reproduce the shader cache request keys.
function(embed_shaders target directory output)
  file(GLOB_RECURSE shaders CONFIGURE_DEPENDS "${directory}/*.slang")
  get_filename_component(parent "${output}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_shaders.py" "${directory}" "${output}"
    DEPENDS ${shaders} "${CMAKE_CURRENT_SOURCE_DIR}/embed_shaders.py" VERBATIM)
  target_sources(${target} PRIVATE "${output}")
endfunction()
embed_shaders(gol_engine "${ROOT}/code/grassland/graphics" "${CMAKE_CURRENT_BINARY_DIR}/grassland/graphics/built_in_shaders.inl")
set_source_files_properties("${ROOT}/code/grassland/graphics/shader.cpp" PROPERTIES
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/grassland/graphics")

# The desktop Game of Life UI, hosted in the native window.
file(GLOB_RECURSE gol_sources CONFIGURE_DEPENDS "${ROOT}/demo/gol/*.cpp")
list(FILTER gol_sources EXCLUDE REGEX "/main\\.cpp$|/game_of_life_lib/")
add_library(gol_ui STATIC ${gol_sources} game/DesktopGameSession.cpp game/image_write.cpp)
target_link_libraries(gol_ui PUBLIC gol_engine freetype)
target_include_directories(gol_ui PRIVATE "${ROOT}/demo/gol" "${ROOT}/demo/gol/game_of_life_lib")
target_compile_definitions(gol_ui PRIVATE LONGMARCH_HOSTED_GAMES GLFW_INCLUDE_NONE)
embed_shaders(gol_ui "${ROOT}/demo/gol" "${CMAKE_CURRENT_BINARY_DIR}/game_gol/built_in_shaders.inl")
set_source_files_properties("${ROOT}/demo/gol/application/application.cpp" PROPERTIES
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/game_gol")

add_library(gameoflife SHARED Bridge.cpp Host.cpp Surface.cpp)
target_link_libraries(gameoflife PRIVATE gol_ui ace_napi.z ace_ndk.z native_window rawfile.z hilog_ndk.z dl)
target_compile_definitions(gameoflife PRIVATE VK_USE_PLATFORM_OHOS)
