# Cross-compiling for another Debian architecture on an x86-64 Debian host,
# with the crossbuild-essential compilers and the target's multiarch libraries:
#   cmake -DCMAKE_TOOLCHAIN_FILE=tools/cross/linux-cross.cmake -DREAKTOR_CROSS_TRIPLE=<triple>
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES REAKTOR_CROSS_TRIPLE)
string(REGEX MATCH "^[^-]+" CMAKE_SYSTEM_PROCESSOR "${REAKTOR_CROSS_TRIPLE}")
set(CMAKE_C_COMPILER ${REAKTOR_CROSS_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${REAKTOR_CROSS_TRIPLE}-g++)
set(CMAKE_LIBRARY_ARCHITECTURE ${REAKTOR_CROSS_TRIPLE})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${REAKTOR_CROSS_TRIPLE}/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
