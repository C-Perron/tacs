project(
    'tacs',
    ['c', 'cpp'],
    'cython',
    version : '0.1.1',
    meson_version: '>=1.4',
    license : 'MIT',
    default_options: [
        'buildtype=release',
        'warning_level=0',
        'c_std=c99',
        'cpp_std=c++11',
        'python.install_env=auto'
    ]
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

if host_machine.system() == 'windows'
    mpi_inc = include_directories(get_option('msmpi-inc'))
    mpi_lib = cpp.find_library(
        'msmpi',
        dirs: get_option('msmpi-lib'),
        has_headers: 'mpi.h',
        header_include_directories: mpi_inc
    )
    mpi_dep = declare_dependency(
        dependencies: mpi_lib,
        include_directories: mpi_inc
    )
else
    mpi_dep = dependency('mpi', language: 'cpp')
endif

blas_dep = dependency('blas')
lapack_dep = dependency('lapack')

metis_inc = include_directories(get_option('metis-inc'))
metis_lib = cc.find_library(
    'metis',
    dirs: get_option('metis-lib'),
    has_headers: 'metis.h',
    header_include_directories: metis_inc
)
metis_dep = declare_dependency(
    dependencies: metis_lib,
    include_directories: metis_inc
)

tacs_extern_dep = declare_dependency(
    dependencies: [mpi_dep, blas_dep, lapack_dep, metis_dep]
)

subdir('src')

subdir('extern/f5tovtk')
subdir('extern/f5totec')
