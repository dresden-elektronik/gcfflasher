#!/bin/sh

TARGET=GCFFlasher

SRCS="main_posix.c gcf.c buffer_helper.c protocol.c u_sstream.c u_strlen.c u_mem.c"
CFLAGS="-Wall -DU_SSTREAM_NO_DEPRECATED -DAPP_VERSION=\"\"\"4.1.0-beta\"\"\""
NEED_LIBDL=0
DEBUG=${debug:-}
CC=${CC:-gcc}
OS=$(uname)

IS_GCC=$(echo "$CC" | grep 'gcc')

# test again to catch compilers which pretend to be GCC (Clang on macOs)
if [ -n "$IS_GCC" ]; then
	IS_GCC=$($CC --version | grep 'gcc')
fi

# debug version
#   debug=1 ./build_linux.sh
#   gdb --args GCFFlasher -d /dev/ttyACM0 -f ...

if [ "$OS" = "Darwin" ]; then
	CFLAGS="$CFLAGS -DPL_MAC"

	FTDI_LIB=$(pkg-config --cflags --libs libftdi1)

	if [ -n "$FTDI_LIB" ]; then
		CFLAGS="$CFLAGS $FTDI_LIB -DHAS_LIBFTDI"
		NEED_LIBDL=1
	else
		echo "libftdi1 not found, building without"
	fi
fi

if [ "$OS" = "Linux" ]; then
	CFLAGS="$CFLAGS -DPL_LINUX"
	# libgpiod is used to reboot the MCU of ConBee I, RaspBee I and RaspBee II.
	# On Debian based distributions the development files can be installed via:
	#   apt install libgpiod-dev

	GPIOD_LIB=$(pkg-config --cflags --libs libgpiod)

	if [ -n "$GPIOD_LIB" ]; then
		CFLAGS="$CFLAGS -DHAS_LIBGPIOD"
		NEED_LIBDL=1
	else
		echo "libgpiod-dev not found, building without"
	fi
fi

if [ -n "$DEBUG" ]; then
	CFLAGS="$CFLAGS -g3 -fsanitize=address,undefined"
else
	CFLAGS="$CFLAGS -O2 -DNDEBUG"
fi

if [ "$NEED_LIBDL" -eq 1 ]; then
	if [ -n "$IS_GCC" ]; then
		CFLAGS="$CFLAGS -Wl,--no-as-needed"
	fi
	CFLAGS="$CFLAGS -ldl"
fi

echo "$CC $CFLAGS"

LANG= $CC $CFLAGS $SRCS -o $TARGET
