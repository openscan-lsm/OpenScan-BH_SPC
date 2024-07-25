# OpenScan device module for Becker & Hickl Single Photon Counters

## How to build

Only x64 builds are curretnly supported.

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

```pwsh
cd path\to\OpenScan-BHSPC
meson setup builddir -Dvcpkgdir=C:\full\path\to\vcpkg -Db_vscrt=static_from_buildtype --buildtype release
meson compile -C builddir
```

This results in the module `OpenScanBHSPC.osdev` in `builddir`.

## Code of Conduct

[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.0-4baaaa.svg)](https://github.com/openscan-lsm/OpenScan/blob/main/CODE_OF_CONDUCT.md)
