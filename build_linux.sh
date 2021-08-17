#!/bin/sh

TARGET=GCFFlasher

SRCS="main_posix.c gcf.c buffer_helper.c protocol.c"
CFLAGS="-Wall -DPL_LINUX"
DEBUG=${debug:-}
CC=${CC:-gcc}
#CC=musl-gcc

# debug version
#   debug=1 ./build_linux.sh
#   gdb --args GCFFlasher -d /dev/ttyACM0 -f ...

if [ -n "$DEBUG" ]; then
	CFLAGS="$CFLAGS -g -O0"
else
	CFLAGS="$CFLAGS -O2 -DNDEBUG"
fi

# libgpiod is used to reboot the MCU of ConBee I, RaspBee I and RaspBee II.
# On Debian based distributions the development files can be installed via:
#   apt install libgpiod-dev

GPIOD_LIB=`pkg-config --cflags --libs libgpiod`

if [ -n "$GPIOD_LIB" ]; then
	CFLAGS="$CFLAGS -DHAS_LIBGPIOD -ldl"
else
	echo "libgpiod-dev not found, building without"
fi

echo "$CC $CFLAGS"

LANG= $CC $CFLAGS $SRCS -o $TARGET
