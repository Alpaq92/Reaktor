# Cross-compiling for FreeBSD on arm64 from a FreeBSD x86-64 host:
#   cmake -DCMAKE_TOOLCHAIN_FILE=tools/cross/freebsd-aarch64.cmake -DREAKTOR_SYSROOT=<dir>
# The sysroot is the arm64 base set plus the packages tools/cross/sysroot.sh gathers.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES REAKTOR_SYSROOT)
set(CMAKE_SYSROOT "${REAKTOR_SYSROOT}")
set(CMAKE_C_COMPILER cc)
set(CMAKE_CXX_COMPILER c++)
string(REGEX MATCH "([0-9]+\\.[0-9]+)" _rel "$ENV{REAKTOR_TARGET_RELEASE}")
if(NOT _rel)
    set(_rel 15.1)
endif()
set(CMAKE_C_COMPILER_TARGET aarch64-unknown-freebsd${_rel})
set(CMAKE_CXX_COMPILER_TARGET aarch64-unknown-freebsd${_rel})
# An archive of arm64 objects needs an archiver that reads them.
find_program(_llvm_ar NAMES llvm-ar llvm-ar19 llvm-ar18)
if(_llvm_ar)
    set(CMAKE_AR "${_llvm_ar}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# pkgconf applies the sysroot only to .pc files found inside it.
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${REAKTOR_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${REAKTOR_SYSROOT}/usr/local/libdata/pkgconfig:${REAKTOR_SYSROOT}/usr/libdata/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH OFF)
