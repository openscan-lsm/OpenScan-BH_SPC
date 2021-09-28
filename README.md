# OpenScan device module for Becker & Hickl Single Photon Counters

## How to build

Currently this module is built with Visual Sutdio 2019. The following
dependencies must be supplied:

- **OpenScanDeviceLib**, part of
  [OpenScanLib](https://github.com/openscan-lsm/OpenScanLib).

- The **BH SPCM DLL**. This should be installed together with the [SPCM
  software](https://www.becker-hickl.com/products/tcspc-package/#product-download)
  in `C:\Program Files (x86)\BH\SPCM\DLL` (even if 64-bit).

- The file `C:\Program Files (x86)\BH\SPCM\SPC_data_file_structure.h`, also
  installed with the BH SPCM software. A patched version of this file is used
  in the build; see instructions below.

- FLIMEvents: This is a header-only library contained in this repository in
  subdirectory `FLIMEvents`. No special steps required.

- **libzip**: We use a static library version. Use the CMake build to install
  at `libzip/$(Platform)/$(Configuration)` under the source directory. See
  instructions below to download and build.

- **zlib**: This is a dependency of libzip. We use a static library, although
  the CMake build will also build a shared library. Use the CMake build to
  install at `zlib/$(Platform)/$(Configuration)` under the source directory.
  See instructions below to download and build.

- **[CMake](https://cmake.org/)** is needed to build zlib and libzip. Install
  the Windows binary from https://cmake.org/download/ if you did not install it
  with the Visual Studio installer.

- The instructions below use **[Ninja](https://ninja-build.org/)** to build
  zlib and libzip. Install the Windows binary from
  https://github.com/ninja-build/ninja/releases if you did not install it with
  the Visual Studio installer.

- Make sure CMake and Ninja are in `PATH`. (Nothing needs to be done if they
  were installed with Visual Studio.)

Detailed steps follow.

### Building OpenScanDeviceLib

Clone OpenScanLib in the same directory containing this project, and build
OpenScanDeviceLib using Visual Studio (for the desired platform and
configurations).

### Patching `SPC_data_file_structure.h`

This header (at least the version tested) does not compile. Run the following
command **in Git Bash** to produce a patched version, which is included by our
code:

```sh
./fix-spc-data-file-structure-h.sh
```

### Building zlib

These instructions are for building a 64-bit version on 64-bit Windows.

From the Start Menu, open **x64 Native Tools Command Prompt for VS 2019**. Run
the following commands.

Make sure to use forward slashes in paths.

```
set OSBH=C:/path/to/OpenScan-BH_SPC
cd /d %OSBH%

git clone https://github.com/madler/zlib.git
cd zlib
git checkout v1.2.9
mkdir build
cd build

cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX=%OSBH%/zlib/x64/Release
ninja
ninja install

cd ..
rmdir /s/q build
mkdir build
cd build

cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_INSTALL_PREFIX=%OSBH%/zlib/x64/Debug
ninja
ninja install
```

(We need to start with an empty build directory when switching configurations,
bucause zlib's CMake build sets the install prefix of internal variables only
the first time we configure.)

For 32-bit builds, use the appropriate version of the VS Command Prompt, and
replace `x64` with `x86` in the install prefix.

### Building libzip

These instructions are for building a 64-bit version on 64-bit Windows. We
build a version that only supports deflate (zlib) compression, since that is
all we need.

From the Start Menu, open **x64 Native Tools Command Prompt for VS 2019**. Run
the following commands.

Make sure to use forward slashes in paths.

```
set OSBH=C:/path/to/OpenScan-BH_SPC
cd /d %OSBH%

git clone https://github.com/nih-at/libzip.git
cd libzip
git checkout rel-1-5-2
mkdir build
cd build

cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX=%OSBH%/libzip/x64/Release ^
  -DBUILD_SHARED_LIBS=NO ^
  -DENABLE_BZIP2=NO ^
  -DENABLE_COMMONCRYPTO=NO ^
  -DENABLE_GNUTLS=NO ^
  -DENABLE_MBEDTLS=NO ^
  -DENABLE_OPENSSL=NO ^
  -DENABLE_WINDOWS_CRYPTO=NO ^
  -DZLIB_LIBRARY_RELEASE=%OSBH%/zlib/x64/Release/lib/zlibstatic.lib ^
  -DZLIB_INCLUDE_DIR=%OSBH%/zlib/x64/Release/include
ninja
ninja install

ninja clean
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_INSTALL_PREFIX=%OSBH%/libzip/x64/Debug ^
  -DBUILD_SHARED_LIBS=NO ^
  -DENABLE_BZIP2=NO ^
  -DENABLE_COMMONCRYPTO=NO ^
  -DENABLE_GNUTLS=NO ^
  -DENABLE_MBEDTLS=NO ^
  -DENABLE_OPENSSL=NO ^
  -DENABLE_WINDOWS_CRYPTO=NO ^
  -DZLIB_LIBRARY_DEBUG=%OSBH%/zlib/x64/Debug/lib/zlibstaticd.lib ^
  -DZLIB_INCLUDE_DIR=%OSBH%/zlib/x64/Debug/include
ninja
ninja install
```

(Of the `ENABLE_*=NO` flags, the important one is Windows Cryptography as
leaving it enabled currently results in a compile error.)

### Building the main project

After the above steps, you should be able to build OpenScanBHSPC in Visual
Studio without any additional configuration.


## Code of Conduct

[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.0-4baaaa.svg)](https://github.com/openscan-lsm/OpenScan/blob/main/CODE_OF_CONDUCT.md)
