cmake_minimum_required (VERSION 3.1)
project (GCFFlasher VERSION 4.1.0)

add_executable(${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
    U_SSTREAM_NO_DEPRECATED
    APP_VERSION="\"\"${PROJECT_VERSION}-beta\"\"")

if(UNIX)
	target_sources(${PROJECT_NAME} PRIVATE main_posix.c gcf.c buffer_helper.c protocol.c u_sstream.c u_strlen.c u_mem.c)

	if (LINUX)
		target_compile_definitions(${PROJECT_NAME} PRIVATE PL_LINUX=1)
		find_package(PkgConfig)
		pkg_check_modules(GPIOD libgpiod)
		if (${GPIOD_FOUND})
			target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_LIBGPIOD)
		endif()
	endif()
endif()


if(MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /utf-8 /std:c99")


    target_compile_definitions(${PROJECT_NAME}PRIVATE
                PL_WIN=1 PL_NO_ESCASCII=1 PL_NO_UTF8=1
    )
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME})

# Debian .deb specifics
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Manuel Pietschmann <mpi@dresden-elektronik.de>")
set(CPACK_DEBIAN_PACKAGE_SECTION "non-free / misc")
set(CPACK_DEBIAN_PACKAGE_)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" LOWERCASE_SYSTEM_NAME)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/dresden-elektronik/gcfflasher")
set(CPACK_PACKAGE_DESCRIPTION "Tool to flash firmware of RaspBee and ConBee.")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}_${PROJECT_VERSION}_${LOWERCASE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")

include(CPack)
