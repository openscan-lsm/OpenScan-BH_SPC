project('OpenScanBHSPC', 'c', 'cpp', default_options: ['warning_level=2'])

cc = meson.get_compiler('c')

if cc.get_id() != 'msvc' and cc.get_id() != 'clang-cl'
    error('Unsupported compiler (requires msvc or clang-cl)')
endif

if build_machine.cpu_family() == 'x86_64'
    programsx86 = 'C:/Program Files (x86)'
else
    programsx86 = 'C:/Program Files'
endif

host_cpu = host_machine.cpu_family()
if host_cpu == 'x86_64'
    spcm_suffix = '64'
elif host_cpu == 'x86'
    spcm_suffix = ''
else
    error(f'Unsupported host CPU family: @host_cpu@')
endif

bh_spcm_dir = programsx86 / 'BH/SPCM'
bh_spcm_inc = include_directories(bh_spcm_dir / 'DLL')

bh_spcm_dep = declare_dependency(
    dependencies: cc.find_library(f'spcm@spcm_suffix@',
        dirs: bh_spcm_dir / f'DLL/LIB/MSVC@spcm_suffix@',
        has_headers: 'Spcm_def.h',
        header_include_directories: bh_spcm_inc,
    ),
    include_directories: bh_spcm_inc,
)

python_prog = find_program('python')
fix_header_script = files('fix-SPC_data_file_structure-header.py')

fixed_headers = custom_target(
    'fix-headers',
    command: [
        python_prog,
        fix_header_script,
        '@INPUT@',
        '@OUTPUT@',
    ],
    input: bh_spcm_dir / 'SPC_data_file_structure.h',
    output: 'SPC_data_file_structure_fixed.h',
)

libzip_dep = dependency(
    'libzip',
    fallback: 'libzip',
    default_options: ['vcpkgdir=' + get_option('vcpkgdir')],
    static: true,
)

rapidjson_dep = dependency(
    'rapidjson',
    fallback: ['rapidjson', 'rapidjson_dep'],
)

ssstr_dep = dependency(
    'ssstr',
    fallback: 'ssstr',
)

shlwapi_dep = cc.find_library('shlwapi')
ws2_32_dep = cc.find_library('Ws2_32')

flimevents_dep = dependency(
    'FLIMEvents',
    fallback: 'FLIMEvents',
)

flimevents_example_inc = subproject('FLIMEvents').get_variable('example_inc')

openscandevicelib_dep = dependency(
    'OpenScanDeviceLib',
    fallback: ['OpenScanLib', 'OpenScanDeviceLib'],
    static: true,
    default_options: [
        'devicelib=enabled',
        'apilib=disabled',
        'docs=disabled',
        'tests=disabled',
    ],
)

src = files(
    'src/AcquisitionControl.cpp',
    'src/BH_SPC150.c',
    'src/BH_SPC150Settings.c',
    'src/DataStream.cpp',
    'src/FIFOAcquisition.cpp',
    'src/RateCounters.cpp',
    'src/SDTFile/SDTFile.c',
    'src/SDTFile/ZipCompress.c',
    'src/UniqueFileName.c',
)

osdev = shared_module(
    'OpenScanBHSPC',
    [
        src,
        fixed_headers,
    ],
    name_suffix: 'osdev',
    c_args: [
        '-DNOMINMAX',
        '-D_CRT_SECURE_NO_WARNINGS',
    ],
    cpp_args: [
        '-DNOMINMAX',
        '-D_CRT_SECURE_NO_WARNINGS',
    ],
    include_directories: [
        include_directories('src'),
        include_directories('src/SDTFile'),
        include_directories('src/Sender'),
    ],
    dependencies: [
        bh_spcm_dep,
        libzip_dep,
        rapidjson_dep,
        ssstr_dep,
        shlwapi_dep,
        ws2_32_dep,
        flimevents_dep,
        openscandevicelib_dep,
    ],
)

replay = executable(
    'ReplaySPC',
    [
        'src/Replay/Replay.cpp',
    ],
    cpp_args: [
        '-DNOMINMAX',
        '-D_CRT_SECURE_NO_WARNINGS',
    ],
    include_directories: [
        include_directories('src'),
        include_directories('src/Sender'),
        flimevents_example_inc,
    ],
    dependencies: [
        rapidjson_dep,
        flimevents_dep,
        ws2_32_dep,
    ],
)
