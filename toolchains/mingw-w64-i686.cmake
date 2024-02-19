# Sample toolchain file for building for Windows from an Ubuntu Linux system.
#
# Typical usage:
#    *) install cross compiler: `sudo apt-get install mingw-w64`
#    *) cd build
#    *) cmake -DCMAKE_TOOLCHAIN_FILE=~/mingw-w64-x86_64.cmake ..
# This is free and unencumbered software released into the public domain.

set(CMAKE_SYSTEM_NAME Windows)
set(TOOLCHAIN_PREFIX i686-w64-mingw32)

# cross compilers to use for C, C++ and Fortran
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_Fortran_COMPILER ${TOOLCHAIN_PREFIX}-gfortran)
set(CMAKE_RC_COMPILER ${TOOLCHAIN_PREFIX}-windres)

# target environment on the build host system
if (EXISTS /usr/${TOOLCHAIN_PREFIX})
    set(CMAKE_FIND_ROOT_PATH /usr/${TOOLCHAIN_PREFIX})
elseif (EXISTS "/opt/${TOOLCHAIN_PREFIX}")
    set(CMAKE_FIND_ROOT_PATH /opt/${TOOLCHAIN_PREFIX})
else()
    message(FATAL "couldn't find root dir")
endif()

# modify default behavior of FIND_XXX() commands
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(CMAKE_C_FLAGS "")
set(CMAKE_EXE_LINKER_FLAGS "")

set(CMAKE_C_FLAGS_RELEASE_INIT "")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "")

set(CMAKE_C_FLAGS_RELEASE  "-O2 -DNDEBUG -flto -fno-builtin -nostdlib -mconsole -Xlinker --stack=0x200000,0x200000")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE  "-Wl,--gc-sections -s")

set(CMAKE_C_FLAGS_DEBUG  "--entry=mainCRTStartup -O0 -g -fno-builtin -nostdlib -mconsole -Xlinker --stack=0x200000,0x200000")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG  "-Wl,--gc-sections")

