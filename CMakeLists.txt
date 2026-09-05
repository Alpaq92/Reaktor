# Curie - one build for Windows, macOS, Linux, the BSDs and (via emcmake) WASM.
#
# This replaces the hand-rolled cl.exe command lines that build.ps1 used to
# assemble. Everything is built from the vendored submodules; nothing is
# expected to be installed on the system.
cmake_minimum_required(VERSION 3.16)
project(curie C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# One CRT for every target: mixing /MD and /MT yields LNK4098 plus unresolved
# __imp_ math symbols, which read as missing libraries rather than a linkage
# mismatch.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "" FORCE)
endif()

set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------- SDL3 -----
# Only video and events are used. Dialogs stay: SDL_ShowOpenFileDialog is the
# portable file picker (FINDINGS 13.1). Trimming these took curie.exe from
# 3.39 MB to 2.66 MB.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
foreach(sub AUDIO JOYSTICK HAPTIC HIDAPI SENSOR CAMERA POWER GPU)
    set(SDL_${sub} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${TP}/SDL" EXCLUDE_FROM_ALL)

# ------------------------------------------------------------ litehtml -----
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/litehtml" EXCLUDE_FROM_ALL)

# ------------------------------------------------ plutovg + plutosvg --------
# Plain C with no build system of their own here; compiled straight from source.
file(GLOB PLUTOVG_SRC "${TP}/plutosvg/plutovg/source/*.c")
add_library(plutovg STATIC ${PLUTOVG_SRC})
target_include_directories(plutovg PUBLIC "${TP}/plutosvg/plutovg/include")
target_compile_definitions(plutovg PUBLIC PLUTOVG_BUILD_STATIC)

add_library(plutosvg STATIC "${TP}/plutosvg/source/plutosvg.c")
target_include_directories(plutosvg PUBLIC "${TP}/plutosvg/source")
target_compile_definitions(plutosvg PUBLIC PLUTOSVG_BUILD_STATIC)
target_link_libraries(plutosvg PUBLIC plutovg)

# ------------------------------------------------------------- shared -------
# Asset loading and the SVG->raster path, used by both the app and the icon
# generator. Deliberately free of SDL and Nuklear so the tool stays small.
add_library(curie_core STATIC
    src/util.c
    src/appicon.c)
target_include_directories(curie_core PUBLIC src)
target_link_libraries(curie_core PUBLIC plutosvg)

# --------------------------------------------------- icon generator ---------
# Renders the Ionicons glyph with the Open-Color palette at build time, so the
# executable's icon is generated from the submodules rather than committed.
add_executable(mkicon tools/mkicon.c)
target_link_libraries(mkicon PRIVATE curie_core)

set(ICON_GLYPH        browsers-outline)
set(ICON_OUTLINE_FAM  violet)
set(ICON_OUTLINE_SH   9)
set(ICON_INSIDE_FAM   indigo)
set(ICON_INSIDE_SH    3)

set(APP_RESOURCES "")
if(WIN32)
    set(GEN "${CMAKE_BINARY_DIR}/gen")
    file(MAKE_DIRECTORY "${GEN}")
    add_custom_command(
        OUTPUT  "${GEN}/curie.ico" "${GEN}/curie.rc"
        COMMAND mkicon "${GEN}/curie.ico" "${GEN}/curie.rc"
                ${ICON_GLYPH} ${ICON_OUTLINE_FAM} ${ICON_OUTLINE_SH}
                ${ICON_INSIDE_FAM} ${ICON_INSIDE_SH}
        DEPENDS mkicon
        COMMENT "Generating application icon from Ionicons + Open-Color"
        VERBATIM)
    set(APP_RESOURCES "${GEN}/curie.rc")
endif()

# ---------------------------------------------------------------- app -------
# WIN32 selects the GUI subsystem; SDL's main shim supplies the entry point.
add_executable(curie WIN32
    src/main.c
    src/nk_impl.c
    src/theme.c
    src/metrics.c
    src/container_nk.cpp
    ${APP_RESOURCES})

target_include_directories(curie PRIVATE
    "${TP}/nuklear"
    "${TP}/nuklear/demo/sdl3_renderer")

target_link_libraries(curie PRIVATE
    curie_core
    SDL3::SDL3-static
    litehtml)

if(MSVC)
    # C5287/C4101 are Nuklear's; C4244/C4267 are plutovg's narrowing warnings.
    target_compile_options(curie PRIVATE /wd5287 /wd4101)
    target_compile_options(plutovg PRIVATE /wd4244 /wd4267 /wd4018)
    target_compile_definitions(curie PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(curie_core PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(mkicon PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The app finds assets by walking up from the executable to the directory
# containing third_party/, so keep the binary inside the tree.
set_target_properties(curie mkicon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
