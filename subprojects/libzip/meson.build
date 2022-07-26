project('libzip', 'c')

message('Falling back to libzip installed via vcpkg')

# I also considered using pkg-config (i.e. dependency()) to find libzip
# installed by vcpkg. That method has the following problems:
# - vckpg's libzip (1.9.0) has an incorrect .pc file (fails to link zlib)
#   (this should be fixable; might be an upstream issue; need to check)
# - pkg_config_path can only be overridden on the command line (or cross/native
#   file), which is inconvenient
# Yet that approach may scale better, since we wouldn't need to do any of the
# stuff below.

if get_option('vcpkgdir') == ''
    error('Option \'vcpkgdir\' not set (pass -Dvcpkgdir=full/path/to/vcpkg)')
endif

vcpkg_prog = find_program(
    'vcpkg',
    dirs: [
        get_option('vcpkgdir'),
    ],
)

vcpkg_triplet = 'x64-windows-static'
vcpkg_prefix = get_option('vcpkgdir') / 'installed' / vcpkg_triplet
vcpkg_inc = include_directories(vcpkg_prefix / 'include')

if get_option('b_vscrt') == 'mtd'
    vcpkg_libdir = vcpkg_prefix / 'debug/lib'
    zlib_name = 'zlibd'
    bz2_name = 'bz2d'
elif get_option('b_vscrt') == 'mt'
    vcpkg_libdir = vcpkg_prefix / 'lib'
    zlib_name = 'zlib'
    bz2_name = 'bz2'
else
    error('Option \'b_vscrt\' must be set to \'mt\' or \'mtd\' to use vcpkg static lib')
endif

run_command(
    vcpkg_prog,
    'install',
    'libzip:' + vcpkg_triplet,
    check: true,
)

cc = meson.get_compiler('c')

libzip_dep = declare_dependency(
    dependencies: [
        cc.find_library('zip',
            dirs: vcpkg_libdir,
            has_headers: 'zip.h',
            header_include_directories: vcpkg_inc,
            static: true,
        ),
        cc.find_library(zlib_name,
            dirs: vcpkg_libdir,
            static: true,
        ),
        cc.find_library(bz2_name,
            dirs: vcpkg_libdir,
            static: true,
        ),
        cc.find_library('advapi32'),
        cc.find_library('bcrypt'),
    ],
    include_directories: vcpkg_inc,
)

meson.override_dependency('libzip', libzip_dep)
