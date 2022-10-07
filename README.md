# GCFFlasher 4

GCFFlasher is the tool to program the firmware of dresden elektronik's Zigbee products.

## Supported Hardware

* ConBee I
* ConBee II
* RaspBee I
* RaspBee II

## Supported platforms

The sources are POSIX compliant with a small platform specific layer, to make porting to different platforms easy.

* GNU/Linux (ARM and AMD64)
* Windows
* macOS

## Notes

* The current release is not yet included in the deCONZ package (ETA is deCONZ 2.13.x release).
* The list command `-l` is in development and only partially implemended.
* The output logging is not streamlined yet.
* On macOS the `-d` parameter is `/dev/cu.usbmodemDE...` where ... is the serialnumber.

## Building on Linux

### Dependencies

The executable can be compiled without any dependencies, but it is recommended to install `libgpiod` to support RaspBee I, RaspBee II and ConBee I.

* A C99 compiler like GCC or Clang
* Linux kernel version 4.8
* pkg-config
* libgpiod

The executable doesn't link directly to libgpiod and will check at runtime if it is available via `dlopen()`.

On Debian based distributions the build dependencies are installed by:

```
apt install pkg-config build-essential libgpiod-dev
```

### Build

1. Checkout this repository

2. Compile the executable with the build script (with GCC)

```
./build_posix.sh
```

**Note:** To use a different compiler use:

```
CC=clang ./build_posix.sh
```

## Building on Windows

### Dependencies

Visual Studio with MSVC C++ compiler needs to be installed. Tested with VS 2022 but older versions should work fine as well.
The executable has no external dependencies.

### Build

1. Checkout this repository

2. Open "x86 Native Tools Command Promt for VS 2022" via Windows Start Menu

3. Navigate to the source directory, e.g. `cd C:\gcfflasher` 

3. Compile the executable with the build script (with MSVC)

```
./build_windows.bat
```

## Building on macOS

### Dependencies

The executable can be compiled without any dependencies, but it is recommended to install `libftdi` to support ConBee I.

* A C99 compiler like GCC or Clang
* The `libftdi` development package can be installed via `brew install libftdi` via [Homebrew](https://brew.sh). 

### Build

1. Checkout this repository

2. Compile the executable with the build script (with Clang)

```
CC=clang ./build_posix.sh
```

## Run

```
$ ./GCFFlasher
GCFFlasher v4.0.0 copyright dresden elektronik ingenieurtechnik gmbh
usage: GCFFlasher <options>
options:
 -r              force device reset without programming
 -f <firmware>   flash firmware file
 -d <device>     device number or path to use, e.g. 0, /dev/ttyUSB0 or RaspBee
 -c              connect and debug serial protocol
 -t <timeout>    retry until timeout (seconds) is reached
 -l              list devices
 -h -?           print this help
```

## Building on FreeBSD

### Build

1. Checkout this repository

2. Compile the executable with the build script (with Clang)

```
CC=cc ./build_posix.sh
```

**Note:** The serial USB device for a ConBee II is `/dev/cuaU0`.


## Differences to previous GCFFlasher version 3.17

* Open sourced under BSD-3-Clause License
* Doesn't require root privileges on Raspberry Pi
* Rewritten in C instead C++
* Smaller binary, with 25 Kb vs. previously 250 Kb on Raspberry PI
* No Qt, libWiringPi and libft2xx (FTDI) dependencies
* Easier to port to different platforms
* Suitable for headless systems and standalone setup which don't use deCONZ
