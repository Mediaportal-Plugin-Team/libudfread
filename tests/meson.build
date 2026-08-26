# Copyright (C) 2026 VLC authors and VideoLAN
# SPDX-License-Identifier: MIT

# Coverage builds need -O0: at -O3 the static functions under test get
# inlined, which mangles gcov line attribution.
coverage_test_options = []
if get_option('b_coverage')
    coverage_test_options += ['optimization=0']
endif

# Unit tests: ecma167.c internals.
test_ecma167 = executable('test_ecma167', 'test_ecma167.c',
    include_directories: '../src',
    override_options: coverage_test_options,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0'])

test('ecma167', test_ecma167,
    suite: 'unit',
    protocol: 'exitcode')

# Unit tests: udf_volume.c internals.
test_udf_volume = executable('test_udf_volume', 'test_udf_volume.c',
    include_directories: '../src',
    override_options: coverage_test_options,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0'])

test('udf_volume', test_udf_volume,
    suite: 'unit',
    protocol: 'exitcode')

# Unit tests: udf_fs internals.
test_udf_fs = executable('test_udf_fs',
    ['test_udf_fs.c', '../src/ecma167.c', '../src/udf_volume.c'],
    include_directories: '../src',
    override_options: coverage_test_options,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0'])

test('udf_fs', test_udf_fs,
    suite: 'unit',
    protocol: 'exitcode')

# Unit tests: udfread.c internals.
test_udfread = executable('test_udfread',
    ['test_udfread.c', '../src/ecma167.c', '../src/udf_volume.c', '../src/udf_fs.c', '../src/default_blockinput.c'],
    include_directories: '../src',
    override_options: coverage_test_options,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0',
             '-DHAVE_UNISTD_H=1', '-DHAVE_FCNTL_H=1'])

test('udfread', test_udfread,
    suite: 'unit',
    protocol: 'exitcode')

# API tests: public udfread.h API against crafted images (tests/udf_image.c),
# linking the real library.
test_udfread_api = executable('test_udfread_api',
    ['test_udfread_api.c', 'udf_image.c'],
    dependencies: libudfread_dep,
    include_directories: ['../src', '../src/udfread'],
    override_options: coverage_test_options,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0'])

test('udfread_api', test_udfread_api,
    suite: 'unit',
    protocol: 'exitcode')

# Conformance tests: crafted images (tests/udf_image.c) against the
# public API via the linked library (udfread_open_input).
test_udf_conformance = executable('test_udf_conformance',
    ['test_udf_conformance.c', 'udf_image.c'],
    dependencies: libudfread_dep,
    c_args: ['-UHAVE_CONFIG_H', '-DHAVE_CONFIG_H=0'])

test('conformance', test_udf_conformance,
    suite: 'conformance',
    protocol: 'exitcode')

# Coverage summary: runs the test binary and prints per-file line coverage.
# Requires the build to be configured with -Db_coverage=true.
run_target('coverage-summary',
    command: [files('coverage.sh'), meson.project_build_root()])

# ABI check (exported functions)
abi_check = find_program('abi_check.sh')

if get_option('default_library') != 'static'
    test('abi-check', abi_check,
        args: [libudfread, meson.project_source_root()],
        suite: 'abi')

    run_target('update-abi',
        command: [abi_check, '--update', libudfread, meson.project_source_root()],
        depends: [libudfread])
endif
