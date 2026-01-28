# OpenScan device module for Becker & Hickl Single Photon Counters

## How to build

Only x64 builds are currently supported.

- Install Visual Studio 2019 or later with C++ Desktop Development,
  [Meson](https://github.com/mesonbuild/meson/releases), and
  [CMake](https://cmake.org/download/). Make sure `meson`, `ninja`, and `cmake`
  are on the `PATH`.
- Install Becker-Hickl's
  [TCSPC Package](https://www.becker-hickl.com/products/tcspc-package/#product-download)
  (make sure SPCM and SPCM-DLL are selected).
- In Git Bash, clone vcpkg (if you do not already have it) and this repo:

```sh
git clone https://github.com/microsoft/vcpkg.git
git clone https://github.com/openscan-lsm/OpenScan-BHSPC.git
```

(Currently, vcpkg is used to obtain libzip conveniently.)

- Bootstrap vcpkg (if you have not done so yet) and install libzip. This should
  be done in PowerShell or Command Prompt, not Git Bash:

```pwsh
cd path\to\vcpkg
.\bootstrap-vcpkg -disableMetrics
.\vcpkg install libzip --triplet=x64-windows-static
```

- Build OpenScan-BHSPC. This is best done in the Developer PowerShell for VS
  2019 (or later), which can be started from the Start Menu (hint: type
  'developer powershell' into the Start Menu to search).

Release build (optimized, no debug info):

```pwsh
cd path\to\OpenScan-BHSPC
meson setup builddir -Dvcpkgdir=C:\full\path\to\vcpkg -Db_vscrt=mt --buildtype release
meson compile -C builddir
```

Debug build (unoptimized, with debug info):

```pwsh
cd path\to\OpenScan-BHSPC
meson setup builddir -Dvcpkgdir=C:\full\path\to\vcpkg -Db_vscrt=mtd --buildtype debug
meson compile -C builddir
```

This results in the module `OpenScanBHSPC.osdev` in `builddir`.

## Usage

`OpenScanBHSPC.osdev` requires two files from the [TCSPC Package](https://www.becker-hickl.com/products/tcspc-package/#product-download) to be available at runtime:
* `C:\Program Files (x86)\BH\SPCM\DLL\spcm64.dll` must be findable by the OS
* `C:\Program Files (x86)\BH\SPCM\scpm.ini` must be findable by this library

The primary use case for this project is within [Micro-Manager](https://micro-manager.org/). To use this library within Micro-Manager, it is easiest to copy `OpenScanBHSPC.osdev` **and** the two additional files into the micro-manager installation folder.

## Emulation

Using the [TCSPC Package](https://www.becker-hickl.com/products/tcspc-package/#product-download), you can emulate a virtual SPC module for use within OpenScan.

To emulate a module, simply launch `C:\Program Files (x86)\BH\SPCM\spcm64.exe`. When the intro window loads, you can uncheck and then check any of the modules (e.g. `M1`), and then click `Apply` to enter emulation mode. Clicking `OK` then starts emulation and launches the main SPCM window.

Note that the device being emulated can be cahnged based on the dropdown at the bottom of the intro window.

> [!IMPORTANT]
> For OpenScan to behave correctly when emulating, you must specify which device is being emualated. To do this, edit `spcm.ini`:
> ```
> simulation = <your device>     ; 0 - hardware mode(default) ,
>                    ; >0 - simulation mode (see spcm_def.h for possible values)
> ```
> The number `<your_device>` comes from `C:\Program Files (x86)\BH\SPCM\DLL\Spcm_def.h` - you can find the list by searching for `possible modes of DLL operation`.

## Code of Conduct

[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.0-4baaaa.svg)](https://github.com/openscan-lsm/OpenScan/blob/main/CODE_OF_CONDUCT.md)
