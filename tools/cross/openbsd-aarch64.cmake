# Cross-compiling for OpenBSD on arm64 from an OpenBSD x86-64 host with the
# ports LLVM (base clang has no arm64 backend):
#   cmake -DCMAKE_TOOLCHAIN_FILE=tools/cross/openbsd-aarch64.cmake -DREAKTOR_SYSROOT=<dir>
# The sysroot is the arm64 sets plus the packages tools/cross/sysroot.sh gathers.
set(CMAKE_SYSTEM_NAME OpenBSD)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES REAKTOR_SYSROOT)
set(CMAKE_SYSROOT "${REAKTOR_SYSROOT}")
set(CMAKE_C_COMPILER /usr/local/bin/clang-21)
set(CMAKE_CXX_COMPILER /usr/local/bin/clang++-21)
string(REGEX MATCH "([0-9]+\\.[0-9]+)" _rel "$ENV{REAKTOR_TARGET_RELEASE}")
if(NOT _rel)
    set(_rel 7.9)
endif()
set(CMAKE_C_COMPILER_TARGET aarch64-unknown-openbsd${_rel})
set(CMAKE_CXX_COMPILER_TARGET aarch64-unknown-openbsd${_rel})
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_AR /usr/local/llvm21/bin/llvm-ar)
set(CMAKE_RANLIB /usr/local/llvm21/bin/llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkgconf applies the sysroot only to .pc files found inside it.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${REAKTOR_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${REAKTOR_SYSROOT}/usr/local/lib/pkgconfig:${REAKTOR_SYSROOT}/usr/X11R6/lib/pkgconfig:${REAKTOR_SYSROOT}/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH OFF)
