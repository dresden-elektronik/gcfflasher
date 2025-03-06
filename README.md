# GCFFlasher 4

GCFFlasher is the tool to program the firmware of dresden elektronik Zigbee products.

## Supported Hardware

* ConBee I
* ConBee II
* ConBee III
* RaspBee I
* RaspBee II
* Hive
* FLS-M

## Supported platforms

The sources are POSIX compliant with a small platform specific layer, to make porting to different platforms easy.

* GNU/Linux (arm, aarch64 and amd64; mips and risc should work too but aren't tested)
* FreeBSD
* Windows
* macOS

## Notes

* To use the sniffer mode on ConBee I and ConBee II the ZShark sniffer firmware needs to be installed. It can be downloaded at https://deconz.dresden-elektronik.de/deconz-firmware
* On macOS the `-d` parameter is `/dev/cu.usbmodemDE...` where ... is the serialnumber.

## Building on Linux

### Dependencies

The executable can be compiled without any dependencies, but it is recommended to install `libgpiod` to support RaspBee I, RaspBee II and ConBee I.

* A C99 compiler like GCC or Clang
* Linux kernel version 4.8
* CMake
* pkg-config
* libgpiod

The executable doesn't link directly to libgpiod and will check at runtime if it is available via `dlopen()`.

On Debian based distributions the build dependencies are installed by:

```
apt install pkg-config build-essential libgpiod-dev cmake make
```

### Build

1. Checkout this repository

2. Navigate to the source directory, e.g. `cd gcfflasher` 

3. Compile the executable with CMake

```
cmake -B build .
cmake --build build
```

The executable is `build/GCFFlasher4`

4. (optional) create a .deb package

```
cd build
cpack -G DEB .
```

## Building on Windows

### Dependencies

Visual Studio with MSVC C++ compiler needs to be installed. Tested with VS 2022 but older versions should work fine as well. The executable has no external dependencies.

### Build

1. Checkout this repository

2. Open "x86 Native Tools Command Promt for VS 2022" via Windows Start Menu

3. Navigate to the source directory, e.g. `cd C:\gcfflasher` 

3. Compile the executable with CMake

```
cmake -B build .
cmake --build build --config Release
```

The executable is `build\Release\GCFFlasher4.exe`.

## Building on macOS

### Dependencies

The executable can be compiled without any dependencies for ConBee II. To support ConBee I the library `libftdi` needs to be installed.

* A C99 compiler like GCC or Clang
* CMake
* (optional) the `libftdi` development package for ConBee I can be installed via `brew install libftdi` using [Homebrew](https://brew.sh). 

### Build

1. Checkout this repository

2. Navigate to the source directory, e.g. `cd gcfflasher` 

3. Compile the executable with CMake

```
cmake -B build .
cmake --build build
```

The executable is `build/GCFFlasher4`

## Run

```
$ ./GCFFlasher4
GCFFlasher 4.8.0 copyright dresden elektronik ingenieurtechnik gmbh
usage: GCFFlasher <options>
options:
 -r              force device reboot without programming
 -f <firmware>   flash firmware file
 -d <device>     device number or path to use, e.g. 0, /dev/ttyUSB0 or RaspBee
 -s <channel>    enable sniffer on Zigbee channel (requires sniffer firmware)
                 the Wireshark sniffer traffic is send to UDP port 17754
 -H <host>       send sniffer traffic to Wireshark running on host
                 default is 172.0.0.1 (localhost)
 -c              connect and debug serial protocol
 -t <timeout>    retry until timeout (seconds) is reached
 -l              list devices
 -x <loglevel>   debug log level 0, 1, 3
 -i              interactive mode for debugging
 -h -?           print this help
```

## Building on FreeBSD

### Build

1. Checkout this repository

2. Navigate to the source directory, e.g. `cd gcfflasher` 

3. Compile the executable with the build script (with Clang)

```
cmake -B build .
cmake --build build
```

The executable is `build/GCFFlasher4`

**Note:** The serial USB device for a ConBee II is `/dev/cuaU0`.


## Differences to previous GCFFlasher version 3.17

* Open sourced under BSD-3-Clause License
* Doesn't require root privileges on Raspberry Pi
* Rewritten in C instead C++
* Smaller binary, with 25 Kb vs. previously 250 Kb + Qt libraries on Raspberry Pi
* No Qt, libWiringPi and libft2xx (FTDI) dependencies
* Easier to port to different platforms
* Suitable for headless systems and standalone setup which don't use deCONZ
