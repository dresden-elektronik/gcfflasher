cmake_minimum_required (VERSION 3.5)
project (GCFFlasher VERSION 4.5.1)

option(USE_NET "Support connection via network sockets" OFF)
set(COMMON_SRCS
        gcf.c
        buffer_helper.c
        protocol.c
        u_bstream.c
        u_sstream.c
        u_strlen.c
        u_mem.c
        net.c
)

add_executable(${PROJECT_NAME} ${COMMON_SRCS})

target_compile_definitions(${PROJECT_NAME}
    PUBLIC
    APP_VERSION="\"\"${PROJECT_VERSION}\"\"")

if (USE_NET)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_NET)
    list(APPEND COMMON_SRCS net_sock.c)
    if (UNIX)
        list(APPEND COMMON_SRCS net_udp_posix.c)
    endif ()
    if (WIN32)
        list(APPEND COMMON_SRCS net_udp_win32.c)
    endif ()
endif ()

if (UNIX)
    target_sources(${PROJECT_NAME} PRIVATE main_posix.c)

    if (CMAKE_BUILD_TYPE MATCHES "Debug")
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wdeprecated)
        target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=undefined -fsanitize=address)
        target_link_options(${PROJECT_NAME} BEFORE PUBLIC -fsanitize=undefined PUBLIC -fsanitize=address)
    endif()

#----------------------------------------------------------------------
    if (${CMAKE_HOST_SYSTEM_NAME} MATCHES "Linux")
        target_compile_definitions(${PROJECT_NAME} PRIVATE PL_LINUX=1)
        target_sources(${PROJECT_NAME}
                PRIVATE
                linux_get_usb_devices.c
                linux_libgpiod_reset.c)

        find_package(PkgConfig)
        pkg_check_modules(GPIOD libgpiod)
        if (${GPIOD_FOUND})

            target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LIBGPIOD)
            target_link_libraries(${PROJECT_NAME} dl)
        endif()
    endif()

#----------------------------------------------------------------------
    if (APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE PL_MAC=1)
        target_sources(${PROJECT_NAME}
                PRIVATE
                macos_get_usb_devices.c)
    endif()
endif()

#----------------------------------------------------------------------
# https://github.com/open-watcom/open-watcom-v2/wiki/OW-tools-usage-with-CMake
if (DOS)
    # if("${CMAKE_C_COMPILER_ID}" MATCHES "OpenWatcom")

    target_compile_definitions(${PROJECT_NAME} PRIVATE
            PL_DOS=1
            PL_NO_ESCASCII=1
            PL_NO_UTF8=1
    )

    set(CMAKE_C_FLAGS "-za99 -w1")
    set(CMAKE_C_STANDARD 99)
    #    add_compile_options(-D_WIN32)

    target_sources(${PROJECT_NAME} PRIVATE ${COMMON_SRCS} main_dos.c)
endif()

if (WIN32)
    option(USE_FTD2XX "Use FTDI ftd2xx library on Windows" OFF)
    target_sources(${PROJECT_NAME} PRIVATE main_windows.c)

    if(MINGW)
        set(CMAKE_VERBOSE_MAKEFILE ON)
    endif()

    if (MSVC)
        set(CMAKE_C_FLAGS_DEBUG_INIT "")
        set(CMAKE_C_FLAGS_DEBUG "/Od /Zi")

#        set(CMAKE_C_FLAGS_MINSIZEREL_INIT "")
#        set(CMAKE_C_FLAGS_MINSIZEREL "/O1 /MT")

        #target_link_options(${PROJECT_NAME} PUBLIC "/NODEFAULTLIB:libcmt")
        target_link_options(${PROJECT_NAME} PUBLIC "/NODEFAULTLIB" "/STACK:0x100000,0x100000")

        add_compile_options(
            $<$<CONFIG:>:/MT> #---------|
            $<$<CONFIG:Debug>:/MTd> #---|-- Statically link the runtime libraries
            $<$<CONFIG:Release>:/MT> #--|
        )

        target_compile_options(${PROJECT_NAME} BEFORE PRIVATE
                "/std:c11"
                "/GR-"
                "/EHa-"
                "/GS-"
                "/Gs999999999"
                )

        if (MSVC_VERSION GREATER_EQUAL 1700)
            set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /utf-8")
        endif()

        if (USE_FTD2XX)
            if ("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
                target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/ftd2xx/Static/i386/ftd2xx.lib)
            else()
                target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/ftd2xx/Static/amd64/ftd2xx.lib)
            endif()
            target_compile_definitions(${PROJECT_NAME} PRIVATE
                    FTD2XX_STATIC
                    USE_FTD2XX
            )
        endif()
    endif()

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        PL_WIN=1
        PL_NO_ESCASCII=1
        PL_NO_UTF8=1
    )

    target_link_libraries(${PROJECT_NAME} setupapi shlwapi advapi32)
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
       RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Debian .deb specifics
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Manuel Pietschmann <mpi@dresden-elektronik.de>")
set(CPACK_DEBIAN_PACKAGE_SECTION "non-free / misc")

# keep arch names as used in the past
set(PKG_ARCH ${CMAKE_SYSTEM_PROCESSOR})
if (${PKG_ARCH} MATCHES "aarch64")
    set(PKG_ARCH "arm64")
endif()
if (${PKG_ARCH} MATCHES "armv7l")
    set(PKG_ARCH "armhf")
endif()
if (${PKG_ARCH} MATCHES "x86_64")
    set(PKG_ARCH "amd64")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" LOWERCASE_SYSTEM_NAME)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/dresden-elektronik/gcfflasher")
set(CPACK_PACKAGE_DESCRIPTION "Tool to flash firmware of RaspBee and ConBee.")
set(CPACK_PACKAGE_FILE_NAME "gcfflasher_${PROJECT_VERSION}_${LOWERCASE_SYSTEM_NAME}_${PKG_ARCH}")

include(CPack)
