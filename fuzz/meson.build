# DotLanth Fuzzing Infrastructure
# Meson build configuration for LibFuzzer-based fuzz targets
#
# Build: meson setup build-fuzz --native-file cross/clang-libcxx.ini -Dfuzzers=true
# Run:   ./build-fuzz/fuzz/dsl_fuzzer corpus/dsl -max_total_time=300

# =============================================================================
# Fuzzer Configuration
# =============================================================================

fuzzer_flags = [
    '-fsanitize=fuzzer,address',
    '-fno-omit-frame-pointer',
    '-O2',
    '-g',
]

# Optional coverage instrumentation for measuring fuzzer effectiveness
if get_option('coverage')
    fuzzer_flags += [
        '-fprofile-instr-generate',
        '-fcoverage-mapping',
    ]
endif

# =============================================================================
# Helper Function
# =============================================================================

# Custom target builder for fuzz targets
# All fuzz targets output to build/fuzz/ directory
fuzz_kwargs = {
    'include_directories': inc_dirs,
    'cpp_args': fuzzer_flags + dotvm_defines,
    'link_args': fuzzer_flags,
}

# =============================================================================
# Existing Fuzz Targets
# =============================================================================

# Bytecode header parsing fuzzer
bytecode_fuzzer = executable('bytecode_fuzzer',
    'bytecode_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# DSL parser fuzzer
dsl_fuzzer = executable('dsl_fuzzer',
    'dsl_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# Assembly parser fuzzer
asm_fuzzer = executable('asm_fuzzer',
    'asm_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# Execution engine fuzzer
execute_fuzzer = executable('execute_fuzzer',
    'execute_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# C API fuzzer (only if C API is enabled)
if get_option('capi')
    capi_fuzzer = executable('capi_fuzzer',
        'capi_fuzzer.cpp',
        link_with: [dotvm_c_lib],
        kwargs: fuzz_kwargs,
    )
endif

# =============================================================================
# New Fuzz Targets (Phase 2)
# =============================================================================

# Replication message fuzzer - tests Raft message deserialization
# Targets: 16-byte header parsing, CRC32 validation, message type dispatch
replication_fuzzer = executable('replication_fuzzer',
    'replication_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# Instruction format fuzzer - tests instruction decoding edge cases
# Targets: 6 instruction types (R, I, S, B, U, J), 32-bit encoding validation
instruction_fuzzer = executable('instruction_fuzzer',
    'instruction_fuzzer.cpp',
    link_with: [dotvm_core_lib],
    kwargs: fuzz_kwargs,
)

# =============================================================================
# Summary Target
# =============================================================================

fuzz_targets = [
    bytecode_fuzzer,
    dsl_fuzzer,
    asm_fuzzer,
    execute_fuzzer,
    replication_fuzzer,
    instruction_fuzzer,
]

if get_option('capi')
    fuzz_targets += [capi_fuzzer]
endif

# Custom target to build all fuzzers
run_target('fuzzers',
    command: ['echo', 'All fuzz targets built'],
    depends: fuzz_targets,
)

message('Fuzzer targets enabled:')
message('  - bytecode_fuzzer (bytecode header parsing)')
message('  - dsl_fuzzer (DSL parser)')
message('  - asm_fuzzer (assembly parser)')
message('  - execute_fuzzer (execution engine)')
message('  - replication_fuzzer (Raft message deserialization) [NEW]')
message('  - instruction_fuzzer (instruction format edge cases) [NEW]')
if get_option('capi')
    message('  - capi_fuzzer (C API)')
endif
