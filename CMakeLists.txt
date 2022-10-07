cmake_minimum_required (VERSION 3.1)
project (GCFFlasher)

# Turn on the ability to create folders to organize projects (.vcproj)
# It creates "CMakePredefinedTargets" folder by default and adds CMake
# defined projects like INSTALL.vcproj and ZERO_CHECK.vcproj
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Set compiler flags and options. 
# Here it is setting the Visual Studio warning level to 4
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /utf-8 /std:c99")

add_executable(GCFFlasher main_windows.c buffer_helper.c gcf.c protocol.c)

target_compile_definitions(GCFFlasher PRIVATE
    PL_WIN=1
	PL_NO_ESCASCII=1
	PL_NO_UTF8=1
)

# Command to output information to the console
# Useful for displaying errors, warnings, and debugging
message ("cxx Flags: " ${CMAKE_CXX_FLAGS})