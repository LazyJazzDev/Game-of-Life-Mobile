cmake_minimum_required(VERSION 3.25)
project(GameOfLife LANGUAGES C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD 17)
option(GOL_PREPARE "Build the macOS shader preparation tool (requires Slang and SPIRV-Cross)" OFF)
option(GOL_APP "Build the SwiftUI application" ON)
set(GOL_HEADERS "" CACHE PATH "Portable dependency headers (vcpkg installed/<triplet>/include)")
set(GAME_OF_LIFE_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../out/ios/GameResources" CACHE PATH
  "Prepared app resources (prepare_resources.py)")
if(NOT APPLE)
  message(FATAL_ERROR "The iOS app requires an Apple SDK")
endif()
if(GOL_PREPARE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  message(FATAL_ERROR "Run shader preparation on macOS")
endif()
get_filename_component(LONGMARCH_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../LongMarch" ABSOLUTE)
set(ROOT "${LONGMARCH_ROOT}")
include("${CMAKE_CURRENT_SOURCE_DIR}/../mobile/Dependencies.cmake")
if(NOT GOL_HEADERS)
  set(GOL_HEADERS "${LONGMARCH_MOBILE_HEADERS}")
endif()
if(NOT EXISTS "${GOL_HEADERS}/fmt/format.h" OR NOT EXISTS "${GOL_HEADERS}/eigen3/Eigen/Core")
  message(FATAL_ERROR "Set GOL_HEADERS to headers installed using mobile/vcpkg.json")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The LongMarch graphics core with its Metal backend, plus the Life rules.
file(GLOB UTIL "${ROOT}/code/grassland/util/*.cpp")
file(GLOB GRAPHICS "${ROOT}/code/grassland/graphics/*.cpp")
file(GLOB METAL "${ROOT}/code/grassland/graphics/backend/metal/*.cpp")
add_library(gol_engine STATIC ${UTIL} ${GRAPHICS} ${METAL}
  "${ROOT}/code/grassland/math/math_mesh.cxx" "${mikktspace_SOURCE_DIR}/mikktspace.c"
  "${ROOT}/demo/gol/game_of_life_lib/game_of_life_lib.cpp")
target_include_directories(gol_engine PUBLIC "${ROOT}/code" "${GOL_HEADERS}" "${GOL_HEADERS}/eigen3"
  "${GOL_HEADERS}/stb" "${metal_cpp_SOURCE_DIR}" "${mikktspace_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "${ROOT}")
target_compile_definitions(gol_engine PUBLIC LONGMARCH_METAL_ENABLED LONGMARCH_HEADLESS
  FMT_HEADER_ONLY NDEBUG "$<$<NOT:$<COMPILE_LANGUAGE:Swift>>:LONGMARCH_ASSETS_DIR=\"\">")
target_link_libraries(gol_engine PUBLIC "-framework Metal" "-framework Foundation" "-framework QuartzCore")
# Generate the embedded Slang on the host, without executing a cross-compiled helper.
set(graphics_generated "${CMAKE_CURRENT_BINARY_DIR}/grassland_graphics")
file(MAKE_DIRECTORY "${graphics_generated}")
file(GLOB_RECURSE graphics_shaders CONFIGURE_DEPENDS "${ROOT}/code/grassland/graphics/*.slang")
add_custom_command(OUTPUT "${graphics_generated}/built_in_shaders.inl"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_shaders.py"
    "${ROOT}/code/grassland/graphics" "${graphics_generated}/built_in_shaders.inl"
  DEPENDS ${graphics_shaders} embed_shaders.py VERBATIM)
target_sources(gol_engine PRIVATE "${graphics_generated}/built_in_shaders.inl")
set_source_files_properties("${ROOT}/code/grassland/graphics/shader.cpp" PROPERTIES
  INCLUDE_DIRECTORIES "${graphics_generated}")
if(GOL_PREPARE)
  longmarch_mobile_compiler(gol_engine)
  # Same exported SPIRV-Cross targets as the desktop Metal backend (LongMarch cmake/Metal.cmake).
  foreach(part core glsl msl)
    find_package(spirv_cross_${part} CONFIG REQUIRED HINTS ${_mobile_prefixes})
  endforeach()
  target_link_libraries(gol_engine PUBLIC spirv-cross-msl spirv-cross-glsl spirv-cross-core)
else()
  target_compile_definitions(gol_engine PRIVATE LONGMARCH_OFFLINE_SHADERS)
endif()

# The desktop Game of Life UI (geometry, controls, animation and its Slang) for a native host.
file(GLOB_RECURSE gol_sources CONFIGURE_DEPENDS "${ROOT}/demo/gol/*.cpp")
list(FILTER gol_sources EXCLUDE REGEX "/main\\.cpp$|/game_of_life_lib/")
add_library(gol_ui STATIC ${gol_sources})
target_link_libraries(gol_ui PUBLIC gol_engine freetype)
target_include_directories(gol_ui PRIVATE "${ROOT}/demo/gol" "${ROOT}/demo/gol/game_of_life_lib")
target_compile_definitions(gol_ui PRIVATE LONGMARCH_HOSTED_GAMES GLFW_INCLUDE_NONE)
set(gol_generated "${CMAKE_CURRENT_BINARY_DIR}/game_gol")
file(MAKE_DIRECTORY "${gol_generated}")
file(GLOB_RECURSE gol_shaders CONFIGURE_DEPENDS "${ROOT}/demo/gol/*.slang")
add_custom_command(OUTPUT "${gol_generated}/built_in_shaders.inl"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_shaders.py"
    "${ROOT}/demo/gol" "${gol_generated}/built_in_shaders.inl"
  DEPENDS ${gol_shaders} embed_shaders.py VERBATIM)
target_sources(gol_ui PRIVATE "${gol_generated}/built_in_shaders.inl")
set_source_files_properties("${ROOT}/demo/gol/application/application.cpp" PROPERTIES
  INCLUDE_DIRECTORIES "${gol_generated}")

# The window-independent host shared by the app and the host checks.
add_library(gol_host STATIC demos/DemoSession.cpp games/DesktopGameSession.cpp games/image_write.cpp)
target_link_libraries(gol_host PUBLIC gol_ui)
target_include_directories(gol_host PRIVATE "${ROOT}/demo/gol/game_of_life_lib" "${ROOT}/demo/gol")

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  # Host renderer: compiles the shaders (GOL_PREPARE=ON) or replays the cache.
  add_executable(gol_check demos/check.cpp)
  target_link_libraries(gol_check PRIVATE gol_host)
  add_executable(gol_games_check tests/games_check.cpp)
  target_link_libraries(gol_games_check PRIVATE gol_host)
  target_include_directories(gol_games_check PRIVATE "${ROOT}/demo/gol" "${ROOT}/demo/gol/game_of_life_lib")
endif()

if(GOL_APP AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    set(swift_target "arm64-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
    string(TOLOWER "${CMAKE_OSX_SYSROOT}" ios_sysroot_lower)
    if(ios_sysroot_lower MATCHES "simulator")
      string(APPEND swift_target "-simulator")
    endif()
    set(CMAKE_Swift_FLAGS "${CMAKE_Swift_FLAGS} -target ${swift_target}")
  endif()
  enable_language(Swift)
  if(NOT EXISTS "${GAME_OF_LIFE_RESOURCES}/Patterns/library.json")
    message(FATAL_ERROR "Prepare GAME_OF_LIFE_RESOURCES first; see README.md")
  endif()
  set(target GameOfLife)
  set(bundle_id net.lazyjazz.gameoflife)
  set(assets "${CMAKE_CURRENT_SOURCE_DIR}/gol/AppIcon.icon")
  set(strings en.lproj/Localizable.strings zh-Hans.lproj/Localizable.strings
    gol/en.lproj/InfoPlist.strings gol/zh-Hans.lproj/InfoPlist.strings)
  # A root folder literally named Resources makes CFBundle interpret an iOS app
  # as a legacy macOS bundle and overlook its root Info.plist.
  set(bundle_resources "${CMAKE_CURRENT_BINARY_DIR}/${target}-bundle/GameResources")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${target}-bundle")
  file(CREATE_LINK "${GAME_OF_LIFE_RESOURCES}" "${bundle_resources}" SYMBOLIC)
  add_executable(${target} MACOSX_BUNDLE gol/GameOfLifeApp.swift games/GamesView.swift
    demos/DemoBridge.mm demos/DemoBridge.h GameBridge.h RenderQueue.h "${bundle_resources}" ${assets} ${strings})
  set_source_files_properties("${bundle_resources}" ${assets} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  # An Icon Composer icon: layered Liquid Glass on iOS 26, flat renderings before.
  set_source_files_properties(${assets} PROPERTIES XCODE_EXPLICIT_FILE_TYPE folder.iconcomposer.icon)
  foreach(file ${strings})
    get_filename_component(language "${file}" DIRECTORY)
    get_filename_component(language "${language}" NAME)
    set_source_files_properties(${file} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/${language})
  endforeach()
  # Folder resources otherwise copy only at configure time; shader preparation can
  # add nested cache files without changing the folder timestamp seen by Ninja.
  add_custom_target(${target}_resources
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${GAME_OF_LIFE_RESOURCES}" "$<TARGET_BUNDLE_DIR:${target}>/GameResources"
    VERBATIM)
  add_dependencies(${target} ${target}_resources)
  target_link_libraries(${target} PRIVATE gol_host "-framework SwiftUI" "-framework CoreGraphics" "-framework MetalKit")
  target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
  set_target_properties(${target} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/gol/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${bundle_id}"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/GameBridge.h"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
    XCODE_ATTRIBUTE_ENABLE_USER_SCRIPT_SANDBOXING NO
    # Xcode archives only install the app; CMake skips installation by default.
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)")
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header;${CMAKE_CURRENT_SOURCE_DIR}/GameBridge.h>")
    target_link_options(${target} PRIVATE -sdk "${CMAKE_OSX_SYSROOT}")
    set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_id}")
    set(ios_platform iphoneos)
    if(ios_sysroot_lower MATCHES "simulator")
      set(ios_platform iphonesimulator)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND xcrun actool "${assets}" --compile "$<TARGET_BUNDLE_DIR:${target}>"
        --platform ${ios_platform} --minimum-deployment-target ${CMAKE_OSX_DEPLOYMENT_TARGET}
        --app-icon AppIcon --target-device iphone --target-device ipad
        --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/${target}-bundle/AppIcon.plist"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/finish_bundle.py"
        "$<TARGET_BUNDLE_DIR:${target}>" "${CMAKE_CURRENT_BINARY_DIR}/${target}-bundle/AppIcon.plist" ${target} ${bundle_id}
      VERBATIM)
  endif()
endif()
