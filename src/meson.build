tacs_inc =include_directories(
    [
        '.',
        './bpmat',
        './elements',
        './elements/a2d',
        './elements/basis',
        './elements/dynamics',
        './elements/shell',
        './constitutive',
        './functions',
        './io'
    ]
)

tacs_src = files(
    [
        'TACSObject.cpp',
        'TacsUtilities.cpp',
        'TACSAssembler.cpp',
        'TACSAuxElements.cpp',
        'TACSCreator.cpp',
        'TACSMg.cpp',
        'TACSBuckling.cpp',
        'TACSAssembler_thread.cpp',
        'TACSIntegrator.cpp',
        'TACSMatrixFreeMat.cpp',
        'TACSContinuation.cpp',
        'TACSSpectralIntegrator.cpp',
    ]
)

subdir('bpmat')
subdir('elements')
subdir('constitutive')
subdir('functions')
subdir('io')

tacs_lib = static_library(
    'tacs',
    tacs_src,
    include_directories: tacs_inc,
    dependencies: tacs_extern_dep,
    install: false,
)

tacs_dep = declare_dependency(
    link_with: tacs_lib,
    include_directories: tacs_inc,
    dependencies: tacs_extern_dep,
)


# Temporary shared library for testing purposes.
shared_library(
    'tacs',
    include_directories: tacs_inc,
    dependencies: tacs_extern_dep,
    install: false,
    link_whole: tacs_lib
)
