# Reaktor - one build for Windows, macOS, Linux, the BSDs and (via emcmake) WASM.
#
# This replaces the hand-rolled cl.exe command lines that build.ps1 used to
# assemble. Everything is built from the vendored submodules; nothing is
# expected to be installed on the system.
cmake_minimum_required(VERSION 3.16)
project(reaktor C CXX)

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
# Only video and events are used. Two stay on purpose: SDL_DIALOG, which File >
# Open on the Popups page calls, and the dummy video driver, which is what lets
# the binary start without a display.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
foreach(sub AUDIO JOYSTICK HAPTIC HIDAPI SENSOR CAMERA POWER GPU TRAY OFFSCREEN)
    set(SDL_${sub} OFF CACHE BOOL "" FORCE)
endforeach()

# And the render backends this app can never end up on. SDL tries its list in
# order and stops at the first that constructs - on Windows that is Direct3D
# 11, which comes first - so Direct3D 9, Direct3D 12 and Vulkan are never
# reached at runtime and cost nothing at startup. What they cost is build time
# and space in the binary, which is the whole of the saving here. Metal and
# the GL drivers stay: they are the first choice on macOS, on Linux and under
# Emscripten.
foreach(drv RENDER_D3D RENDER_D3D12 RENDER_VULKAN VULKAN)
    set(SDL_${drv} OFF CACHE BOOL "" FORCE)
endforeach()

# And OpenGL, on the free desktops only, which is the largest single thing in
# this process's memory and buys it nothing.
#
# Nothing under src/ calls a GL function or asks for an SDL_WINDOW_OPENGL
# window; the renderer is SDL's software rasteriser by default (see the
# REAKTOR_RENDERER block in src/main.c). But SDL's X11 driver initialises GL
# whatever the renderer is, and on a desktop without a GPU that GL is Mesa's
# llvmpipe - which is itself a software rasteriser, reached the expensive way.
# Measured here, same binary, same page, only this flag between them:
#
#     SDL with GL      RSS 123.6 MB   private 58.7 MB   heap 31.1 MB
#     SDL without GL   RSS  11.4 MB   private  4.3 MB   heap  1.1 MB
#
# 53 MB of that is libLLVM and 10 MB libgallium; the 30 MB of heap is what
# llvmpipe allocates on the way up. The figure without it is finally the one
# docs/PERFORMANCE.md reports for Windows, where there is no Mesa to load.
#
# The cost is Mesa's, so the switch is scoped to where Mesa is: Windows keeps
# direct3d11 and macOS keeps Metal, neither of which is affected, and
# Emscripten keeps GLES because WebGL is the only GPU it has. What it costs
# here is REAKTOR_RENDERER=gpu, which on a free desktop now has no driver to
# pin - configure with -DREAKTOR_SDL_GL=ON to get it back for a measurement.
option(REAKTOR_SDL_GL "Build SDL's OpenGL/GLES drivers on Linux and the BSDs" OFF)
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT REAKTOR_SDL_GL)
    set(SDL_OPENGL   OFF CACHE BOOL "" FORCE)
    set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
endif()

# And the one X11 extension whose absence stopped the build outright. SDL wants
# XScreenSaver's *headers* at configure time and fails hard without them, while
# the only thing behind them is SDL_DisableScreenSaver - which this app never
# reaches, because main.c sets SDL_HINT_VIDEO_ALLOW_SCREENSAVER on purpose: an
# app that draws nothing at rest has no business holding the display awake.
# So the dependency was being paid for a code path that is already dead here,
# and libxss-dev is not installed by default on a Debian desktop. Inert on
# Windows, macOS and Emscripten, where SDL never looks at the variable.
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)

add_subdirectory("${TP}/SDL" EXCLUDE_FROM_ALL)

# --------------------------------------------------------- LCUI libcss -----
# Two of LCUI's libraries, and only those: yutil (containers and strings) and
# libcss (parser, selector matching, cascade). Nothing else.
#
# libui, pandagl and libthread are deliberately not built. They were, while the
# document engine laid out and painted an HTML page; Nuklear does both now, so
# libui's layout and pandagl's rasteriser are dead weight - about 23k lines of
# it - and dropping pandagl removes the FreeType question entirely.
#
# What remains is used as a stylesheet engine: parse CSS once, then ask it for
# the computed values behind a selector like "button.pure-button:hover" and
# push those into Nuklear's style before drawing the widget. See src/style.c.
#
# LCUI builds with xmake upstream, which generates a config.h per module from a
# .in template. Those are produced here instead, into the build tree, so the
# submodule is never written to. Both modules include a bare "config.h", so the
# two are concatenated into one header on the include path - their macro
# prefixes do not collide.
set(LC       "${TP}/lcui/lib")
set(LCUI_CFG "${CMAKE_BINARY_DIR}/gen/lcui")
file(MAKE_DIRECTORY "${LCUI_CFG}")

set(LCUI_FEATURES YUTIL_STATIC_BUILD LIBCSS_STATIC_BUILD)

# Expands one xmake template: "${define X}" becomes a real #define when X was
# selected above, and a comment when it was not. The version macros are inert
# here - nothing reads them - so they are filled with a placeholder rather than
# copied out of the submodule's build files.
function(lcui_expand IN OUTVAR)
    file(READ "${IN}" _c)
    string(REGEX MATCHALL "\\$\\{define [A-Z_]+\\}" _tokens "${_c}")
    foreach(_t IN LISTS _tokens)
        string(REGEX REPLACE "\\$\\{define ([A-Z_]+)\\}" "\\1" _name "${_t}")
        list(FIND LCUI_FEATURES "${_name}" _found)
        if(_found GREATER -1)
            set(_r "#define ${_name} 1")
        else()
            set(_r "/* ${_name}: not enabled by this build */")
        endif()
        string(REPLACE "${_t}" "${_r}" _c "${_c}")
    endforeach()
    string(REPLACE "\${VERSION_MAJOR}" "0" _c "${_c}")
    string(REPLACE "\${VERSION_MINOR}" "0" _c "${_c}")
    string(REPLACE "\${VERSION_ALTER}" "0" _c "${_c}")
    string(REPLACE "\${VERSION}" "vendored" _c "${_c}")
    set(${OUTVAR} "${_c}" PARENT_SCOPE)
endfunction()

set(LCUI_MERGED "/* Generated by CMakeLists.txt from the .in templates in\n   third_party/lcui. Do not edit; do not commit. */\n")
foreach(_in "${LC}/yutil/include/yutil/config.h.in"
            "${LC}/css/src/config.h.in")
    lcui_expand("${_in}" _part)
    string(APPEND LCUI_MERGED "\n${_part}")
endforeach()
file(WRITE "${LCUI_CFG}/config.h" "${LCUI_MERGED}")

file(GLOB_RECURSE LCUI_SRC
    "${LC}/yutil/src/*.c"
    "${LC}/css/src/*.c")

add_library(lcui STATIC ${LCUI_SRC})
target_include_directories(lcui PUBLIC
    "${LCUI_CFG}"
    "${LC}/yutil/include"
    "${LC}/css/include")
if(MSVC)
    # Upstream builds with -Wall -Werror under GCC; MSVC's diagnostics are a
    # different set entirely and none of them are ours to fix.
    target_compile_options(lcui PRIVATE /w)
    target_compile_definitions(lcui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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

# -------------------------------------------------------------- onlay -------
# The layout engine: it computes rectangles and nothing else - no widgets, no
# drawing, no input, and no knowledge of the program using it. Forked from
# randrew/layout and converted to plain C99, because upstream needs C++ under
# MSVC; see onlay/README.md.
#
# In-tree while the sample drives its API, and written to leave as its own
# repository afterwards - which is why it includes nothing from the rest of the
# tree and is linked here exactly like a third-party library.
add_library(onlay STATIC onlay/onlay.c)
target_include_directories(onlay PUBLIC onlay)
# Onlay stores coordinates as int16_t unless told otherwise. Nuklear draws in
# floats and the accessibility dumps compare bounds exactly, so a truncating
# layout would drift from the pixels it is supposed to reproduce. A definition
# rather than an edit, so the fork's diff against upstream stays what it is.
# (=1 rather than bare: upstream tests it both with #ifdef and #if ==1.)
target_compile_definitions(onlay PUBLIC LAY_FLOAT=1)

# Checked against the login card's real geometry, with the expected numbers
# taken from the running app's accessibility dump rather than from onlay
# itself. Built by default so it cannot rot silently:
#     build/onlaytest
if(NOT EMSCRIPTEN)
    add_executable(onlaytest onlay/test_onlay.c)
    target_link_libraries(onlaytest PRIVATE onlay)
    set_target_properties(onlaytest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# ------------------------------------------------------------- shared -------
# Asset loading and the SVG->raster path, used by both the app and the icon
# generator. Deliberately free of SDL and Nuklear so the tool stays small.
add_library(reaktor_core STATIC
    core/base/util.c
    core/render/appicon.c)
target_include_directories(reaktor_core PUBLIC core/base core/render)
target_link_libraries(reaktor_core PUBLIC plutosvg)

# ------------------------------------------------------------ icon ----------
# Explorer reads the icon from a linked Win32 resource, so it has to exist
# before the linker runs. branding/ holds it committed for that reason: the
# .svg beside it is the authored mark and the .ico is derived from it, by a
# tool that is not part of the build.
#     cmake --build build --target mkicon && build/mkicon
set(APP_ICON branding/reaktor-icon.ico)
set(APP_RESOURCES "")

add_executable(mkicon EXCLUDE_FROM_ALL tools/mkicon.c)
target_link_libraries(mkicon PRIVATE reaktor_core)
set_target_properties(mkicon PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Phase 1 of docs/ACCESSIBILITY.md: the shadow-tree model and its diff, with no
# platform behind it and nothing instrumented yet. Built by default rather than
# on demand so it cannot rot silently - nothing else compiles src/a11y.c until
# phase 2 - and run by hand:
#     build/a11ytest
# Chord matching, which the accessibility dumps cannot reach: a keypress is
# not a frame. Built by default for the same reason as the others:
#     build/keytest
if(NOT EMSCRIPTEN)
    add_executable(keytest tools/keytest.c core/ui/keys.c)
    target_include_directories(keytest PRIVATE core/ui)
    target_link_libraries(keytest PRIVATE SDL3::SDL3-static)
    set_target_properties(keytest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

if(NOT EMSCRIPTEN)
    add_executable(a11ytest tools/a11ytest.c core/a11y/a11y.c)
    target_include_directories(a11ytest PRIVATE core/a11y core/render)
    set_target_properties(a11ytest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# The Onlay shim: ids in, rects out, one frame later. Onlay's own arithmetic is
# onlaytest's job; this is what the shim owes on top of it:
#     build/laytest
if(NOT EMSCRIPTEN)
    # The easing curves and the table that remembers a value, with no window
    # and no frame - which is what dropping App out of anim.h bought.
    add_executable(animtest tools/animtest.c core/anim/anim.c)
    target_include_directories(animtest PRIVATE core/anim core/a11y core/render
                                                platform)
    target_link_libraries(animtest PRIVATE SDL3::SDL3)
    set_target_properties(animtest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_executable(laytest tools/laytest.c core/ui/layout.c)
    target_include_directories(laytest PRIVATE core/ui core/render)
    target_link_libraries(laytest PRIVATE onlay)
    set_target_properties(laytest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()

# Attributes another process's private bytes to named buckets. A diagnostic
# for the maintainer rather than part of the app - not built by default, and
# not on the web, where there is no address space to walk:
#     cmake --build build --target vmwalk
if(WIN32 AND NOT EMSCRIPTEN)
    add_executable(vmwalk EXCLUDE_FROM_ALL tools/vmwalk.c)
endif()

if(WIN32)
    # The .rc is written here rather than committed because it is two lines of
    # boilerplate; the icon it names is copied next to it so rc.exe resolves
    # the reference without a search path. Resource id 1: the lowest-numbered
    # ICON is the one Explorer displays.
    set(GEN "${CMAKE_BINARY_DIR}/gen")
    file(MAKE_DIRECTORY "${GEN}")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${APP_ICON}"
                   "${GEN}/reaktor.ico" COPYONLY)
    file(WRITE "${GEN}/reaktor.rc" "1 ICON \"reaktor.ico\"
")
    set(APP_RESOURCES "${GEN}/reaktor.rc")
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    # The same job as the .rc above, done the way the free desktops do it.
    #
    # An ELF file has no icon resource. There is nowhere in the format to put
    # one, so a file manager draws every executable with the same generic glyph
    # and no flag on this build changes that - the .ico above is a Windows
    # resource and reaches nothing here. What a free desktop reads instead is a
    # .desktop entry, which names the binary and the icon by path: the icon
    # belongs to the *application* rather than to the file, and turns up in the
    # menu, in the switcher and beside the window. StartupWMClass is what ties
    # a running window back to the entry - SDL sets WM_CLASS from argv[0],
    # which makes it "reaktor".
    #
    # Absolute paths, because the binary stays in the build tree (see the note
    # on reaktor_root at the foot of this file), and generated rather than
    # committed for the same reason the .rc is: it is boilerplate, and a
    # committed copy would name whoever built it last. Nothing installs it -
    # that is the user's directory, not the build's:
    #
    #     cp build/reaktor.desktop ~/.local/share/applications/
    file(WRITE "${CMAKE_BINARY_DIR}/reaktor.desktop"
"[Desktop Entry]
Type=Application
Version=1.0
Name=Reaktor
Comment=A Nuklear-powered engine where none of the styling is compiled in
Exec=${CMAKE_BINARY_DIR}/reaktor
Icon=${CMAKE_CURRENT_SOURCE_DIR}/branding/reaktor-icon.png
Terminal=false
Categories=Development;
StartupWMClass=reaktor
")
endif()

# ---------------------------------------------------------------- app -------
# WIN32 selects the GUI subsystem; SDL's main shim supplies the entry point.
# The rule the tree follows, which is about size rather than about platforms:
# a whole platform subsystem gets its own file under src/sys/, built only
# where it applies - phase 4 of docs/ACCESSIBILITY.md is the one that exists.
# A small platform fact stays inline behind #ifdef in the file that needs it:
# src/util.c (the executable's path, the memory counters), src/theme.c (the
# desktop's colour scheme) and src/main.c (the browser's missing window frame,
# a DXGI query, a macOS event quirk).
# a11y_snapshot.c is portable and lives here anyway: it is the half of a
# platform bridge that is not platform-specific, and it is only built when
# there is a bridge to use it.
set(SYS_SRC "")
set(SYS_LIBS "")
set(SYS_DEFS "")
if(WIN32 AND NOT EMSCRIPTEN)
    list(APPEND SYS_SRC platform/a11y/a11y_snapshot.c platform/a11y/a11y_win32.c)
elseif(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    # Linux and the BSDs both: AT-SPI is a freedesktop standard, not a Linux
    # one, and nothing in the bridge is Linux-specific - hence UNIX AND NOT
    # APPLE rather than a check for Linux.
    #
    # AT-SPI is D-Bus, spoken directly rather than through ATK - see the head
    # of a11y_atspi.c. libdbus is taken under its Academic Free License 2.1
    # arm, which is MIT-shaped; NOTICE.md carries the attribution. Without it
    # the no-op in a11y_web.c stands and the app is simply not served, which
    # is the same thing that happens on a desktop with no accessibility bus.
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(DBUS QUIET dbus-1)
    endif()
    if(DBUS_FOUND)
        list(APPEND SYS_SRC platform/a11y/a11y_snapshot.c platform/a11y/a11y_atspi.c)
        list(APPEND SYS_LIBS ${DBUS_LIBRARIES})
        list(APPEND SYS_DEFS REAKTOR_HAVE_ATSPI)
        message(STATUS "a11y: AT-SPI over libdbus ${DBUS_VERSION}")
    else()
        message(STATUS "a11y: no dbus-1; the AT-SPI bridge is not built")
    endif()
elseif(APPLE AND NOT EMSCRIPTEN)
    # The project's first Objective-C, and the only file that is not C:
    # NSAccessibility is a set of Objective-C protocols with no C entry point.
    # ARC, because an element per node is the one place here that has object
    # lifetimes at all, and a leak on a platform this cannot be tested on is
    # not worth the retain counting.
    enable_language(OBJC)
    list(APPEND SYS_SRC platform/a11y/a11y_snapshot.c platform/a11y/a11y_macos.m)
    list(APPEND SYS_DEFS REAKTOR_HAVE_NSACCESSIBILITY)
    set_source_files_properties(platform/a11y/a11y_macos.m PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    list(APPEND SYS_LIBS "-framework Cocoa")
endif()

add_executable(reaktor WIN32
    core/a11y/a11y.c
    core/anim/anim.c
    core/base/metrics.c
    core/css/style.c
    core/css/cssflat.c
    core/render/nk_impl.c
    core/render/draw.c
    core/ui/declare.c
    core/ui/focus.c
    core/ui/keys.c
    core/ui/layout.c
    core/ui/style_map.c
    core/ui/widgets.c
    platform/a11y/a11y_web.c
    platform/theme.c
    ${SYS_SRC}
    runtime/app.c
    samples/shell.c
    samples/shortcuts.c
    samples/showcase.c
    ${APP_RESOURCES})

target_include_directories(reaktor PRIVATE
    core/a11y
    core/anim
    core/base
    core/css
    core/render
    core/ui
    platform
    platform/a11y
    samples
    src
    "${TP}/nuklear"
    "${TP}/nuklear/demo/sdl3_renderer")

target_link_libraries(reaktor PRIVATE
    reaktor_core
    SDL3::SDL3-static
    onlay
    lcui)
# uiautomationcore is the UIA provider API (phase 4b); ole32 and oleaut32 are
# the COM and BSTR halves of it.
if(WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(reaktor PRIVATE uiautomationcore ole32 oleaut32)
endif()
if(SYS_LIBS)
    target_link_libraries(reaktor PRIVATE ${SYS_LIBS})
endif()
if(DBUS_FOUND)
    target_include_directories(reaktor PRIVATE ${DBUS_INCLUDE_DIRS})
endif()
if(SYS_DEFS)
    target_compile_definitions(reaktor PRIVATE ${SYS_DEFS})
endif()

# dxguid supplies IID_IDXGIDevice, which is how the app asks the renderer SDL
# gave it whether Direct3D is talking to a real GPU - see renderer_is_software
# in src/main.c. Nothing else in the tree touches Direct3D.
if(WIN32 AND NOT EMSCRIPTEN)
    target_link_libraries(reaktor PRIVATE dxguid)
endif()

# ------------------------------------------------------------- wasm --------
# The same sources, linked to a page instead of an executable.
#
# Two things have to change and only two. The assets are read from the
# submodules at runtime by path, so they are packaged into Emscripten's
# virtual filesystem at exactly the paths the code already asks for - see
# reaktor_root() in src/util.c, which answers "" here for that reason. And the
# window is a canvas, so there is no frame to remove and no frame to draw:
# main.c defaults REAKTOR_BORDERLESS off under __EMSCRIPTEN__.
#
# Everything else - SDL3, libcss, plutovg, Nuklear, the CSS seam - is portable
# C and comes across untouched, which was the point of the callback main loop
# in the first place.
if(EMSCRIPTEN)
    # -Oz on the compile side too; CMAKE_BUILD_TYPE=Release would otherwise
    # hand every translation unit -O3, which optimises for speed.
    target_compile_options(reaktor PRIVATE -Oz)
    target_compile_options(lcui PRIVATE -Oz)
    target_compile_options(plutovg PRIVATE -Oz)
    target_compile_options(plutosvg PRIVATE -Oz)
    target_compile_options(reaktor_core PRIVATE -Oz)

    # Every asset the app opens is named by a string literal in src/, so
    # the package is built from those rather than from a directory: assets/fonts
    # holds several faces against the one that is loaded, and a hand-written
    # list would go stale the first time a path changed. The branding SVG comes
    # along the same way - it is the window icon.
    set(WASM_ASSETS "")
    # Every directory holding app or library sources: an asset named in one
    # of them and not scanned here is a white quad on the page and says
    # nothing about it.
    file(GLOB _reaktor_srcs "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/core/*/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/core/*/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.h")
    foreach(_s IN LISTS _reaktor_srcs)
        # \. and not \. : CMake unescapes a quoted argument before the regex
        # engine sees it, so \. arrives as a bare dot, which matches anything.
        # That was harmless while the extensions were css|ttf|otf, and stopped
        # being harmless the moment svg was added - "ionicons/src/svg" then
        # matched as an asset, and emcc preloads a directory whole. The web
        # bundle went from 47 KB of data to 903 KB, all 1357 icons of it.
        file(STRINGS "${_s}" _lines REGEX "(third_party|assets|branding)/[^\"]*\\.(css|ttf|otf|svg|png)")
        foreach(_l IN LISTS _lines)
            string(REGEX MATCHALL "(third_party|assets|branding)/[A-Za-z0-9_/.-]*\\.(css|ttf|otf|svg|png)"
                   _paths "${_l}")
            list(APPEND WASM_ASSETS ${_paths})
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES WASM_ASSETS)

    # The Ionicons submodule is 1357 files and 3.6 MB; about forty of them are
    # ever drawn. Their names are string literals in src/, so they are found
    # rather than listed - a hand-written list would be wrong the first time a
    # page gained an icon.
    #
    # Two patterns, because a name reaches the source two ways and the EXISTS
    # test below decides between the candidates. Most are written inside a
    # longer literal - "chevron-down-outline.svg?stroke=%s" - and only the
    # "-outline" suffix can be found in the middle of one without matching
    # every identifier in the file. The circles broke that: a filled disc is
    # "ellipse" and a radio's rim "radio-button-off", so a name that is the
    # whole literal counts too. A missing icon draws as a white quad and says
    # nothing, which is what one pattern alone cost. The second picks up a few
    # literals that happen to share a name with an icon - "list", "menu",
    # "card" - about two kilobytes of glyphs nobody draws.
    # Every directory holding app or library sources: an asset named in one
    # of them and not scanned here is a white quad on the page and says
    # nothing about it.
    file(GLOB _reaktor_srcs "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/core/*/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/core/*/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
                      "${CMAKE_CURRENT_SOURCE_DIR}/platform/*/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.c"
                      "${CMAKE_CURRENT_SOURCE_DIR}/samples/*.h")
    set(WASM_ICONS "")
    foreach(_s IN LISTS _reaktor_srcs)
        file(STRINGS "${_s}" _lines REGEX "[a-z0-9-]+-outline")
        foreach(_l IN LISTS _lines)
            string(REGEX MATCHALL "[a-z0-9]+(-[a-z0-9]+)*-outline" _n "${_l}")
            list(APPEND WASM_ICONS ${_n})
        endforeach()
        file(STRINGS "${_s}" _lines REGEX "\"[a-z0-9]+(-[a-z0-9]+)*\"")
        foreach(_l IN LISTS _lines)
            string(REGEX MATCHALL "\"[a-z0-9]+(-[a-z0-9]+)*\"" _n "${_l}")
            foreach(_q IN LISTS _n)
                string(REPLACE "\"" "" _q "${_q}")
                list(APPEND WASM_ICONS "${_q}")
            endforeach()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES WASM_ICONS)
    list(LENGTH WASM_ASSETS _asset_count)

    # SHELL: keeps CMake from de-duplicating the repeated --preload-file flag
    # and leaving one flag in front of every path, which emcc then reads as
    # input files it cannot find.
    set(WASM_PRELOAD "")
    foreach(_a IN LISTS WASM_ASSETS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_a}")
            list(APPEND WASM_PRELOAD
                 "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/${_a}@/${_a}")
        endif()
    endforeach()
    set(_icon_count 0)
    foreach(_i IN LISTS WASM_ICONS)
        set(_rel "third_party/ionicons/src/svg/${_i}.svg")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_rel}")
            list(APPEND WASM_PRELOAD
                 "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/${_rel}@/${_rel}")
            math(EXPR _icon_count "${_icon_count} + 1")
        endif()
    endforeach()
    message(STATUS "wasm: packaging ${_asset_count} assets and ${_icon_count} icons")

    set_target_properties(reaktor PROPERTIES SUFFIX ".html")
    target_link_options(reaktor PRIVATE
        ${WASM_PRELOAD}
        # The font atlas and the SVG rasters are allocated as the UI is first
        # drawn, so the heap has to be able to grow past whatever is reserved.
        -sALLOW_MEMORY_GROWTH=1
        -sINITIAL_MEMORY=64MB
        -sSTACK_SIZE=4MB
        # SDL3's renderer targets GLES2/WebGL here.
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sEXIT_RUNTIME=0
        # Size, not speed: this is a download. -Oz on the link line is what
        # runs Binaryen's wasm-opt over the module - Binaryen is not an
        # alternative to Emscripten, it is the part of it that does this.
        -Oz
        --closure 1
        "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/tools/shell.html")
    set_property(TARGET reaktor APPEND PROPERTY LINK_DEPENDS
                 "${CMAKE_CURRENT_SOURCE_DIR}/tools/shell.html")

    # Favicon: the browser fetches /favicon.ico from the server root; the
    # brand SVG plus the same PNG the .app bundle uses are dropped next to
    # reaktor.html so the <link rel="icon"> in shell.html can reach them.
    # The .svg is served as the primary and the .png as the alternate for
    # any client that will not draw SVG icons.
    add_custom_command(TARGET reaktor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/branding/reaktor-icon.svg"
            "${CMAKE_BINARY_DIR}/reaktor-icon.svg"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/branding/reaktor-icon.png"
            "${CMAKE_BINARY_DIR}/reaktor-icon.png"
        COMMENT "Copying favicon assets"
        VERBATIM)
endif()

if(MSVC)
    # C5287/C4101 are Nuklear's; C4244/C4267 are plutovg's narrowing warnings.
    target_compile_options(reaktor PRIVATE /wd5287 /wd4101)
    target_compile_options(plutovg PRIVATE /wd4244 /wd4267 /wd4018)
    target_compile_definitions(reaktor PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(reaktor_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# The app finds assets by walking up from the executable to the directory
# containing third_party/, so keep the binary inside the tree.
set_target_properties(reaktor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ------------------------------------------------------------- macOS .app --
# Assemble build/reaktor.app around the plain binary, alongside it. Both are
# runnable: `./build/reaktor` from the command line as before, and
# `./build/reaktor.app` from Finder without a Terminal. They share one copy
# of the executable file, and the bundle carries the asset tree the walk-up
# would otherwise need - see reaktor_root() in src/util.c, which prefers
# Contents/Resources when it detects a bundle layout in argv[0].
if(APPLE AND NOT EMSCRIPTEN)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/Info.plist.in"
        "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)

    add_custom_command(TARGET reaktor POST_BUILD
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/make-bundle.sh"
                "${CMAKE_BINARY_DIR}/reaktor.app"
                "$<TARGET_FILE:reaktor>"
                "${CMAKE_BINARY_DIR}/Info.plist"
                "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Assembling reaktor.app"
        VERBATIM)
endif()
