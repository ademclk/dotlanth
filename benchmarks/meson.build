# DotLanth Benchmarks
# Meson build configuration for performance benchmarks
#
# Build: meson setup build-bench --native-file cross/clang-libcxx.ini -Dbenchmarks=true --buildtype=release
# Run:   ./build-bench/benchmarks/arithmetic_bench

# =============================================================================
# Benchmark Configuration
# =============================================================================

bench_flags = dotvm_warning_flags + ['-O3', '-DNDEBUG']

# =============================================================================
# Custom Benchmark Support Library
# =============================================================================

dotvm_bench_lib = static_library('dotvm_bench',
    '../src/dotvm/core/bench/benchmark_config.cpp',
    include_directories: inc_dirs,
    cpp_args: bench_flags,
)

# =============================================================================
# Google Benchmark-based Benchmarks
# =============================================================================

# Arithmetic benchmark (uses Google Benchmark)
arithmetic_bench = executable('arithmetic_bench',
    'arithmetic_bench.cpp',
    include_directories: inc_dirs,
    cpp_args: bench_flags,
    link_with: [dotvm_core_lib],
    dependencies: [benchmark_dep, benchmark_main_dep],
)

# =============================================================================
# Custom Benchmark Suite
# =============================================================================

# SIMD benchmark
simd_benchmark = executable('dotvm_simd_benchmark',
    'simd_benchmark.cpp',
    include_directories: inc_dirs,
    cpp_args: bench_flags,
    link_with: [dotvm_core_lib, dotvm_bench_lib],
)

# BigInt benchmark
bigint_benchmark = executable('dotvm_bigint_benchmark',
    'bigint_benchmark.cpp',
    include_directories: inc_dirs,
    cpp_args: bench_flags,
    link_with: [dotvm_core_lib, dotvm_bench_lib],
)

# Crypto benchmark
crypto_benchmark = executable('dotvm_crypto_benchmark',
    'crypto_benchmark.cpp',
    include_directories: inc_dirs,
    cpp_args: bench_flags,
    link_with: [dotvm_core_lib, dotvm_bench_lib],
)

# =============================================================================
# TOOL-010: Benchmark Suite CLI
# =============================================================================

if get_option('cli')
    dotvm_bench_cli = executable('dotvm_bench',
        'vm_bench.cpp',
        'bench_runner.cpp',
        '../src/dotvm/cli/bench_cli_app.cpp',
        '../src/dotvm/cli/bench_formatter.cpp',
        '../src/dotvm/cli/bench_main.cpp',
        '../src/dotvm/cli/commands/bench_command.cpp',
        include_directories: inc_dirs,
        cpp_args: bench_flags + dotvm_defines,
        link_with: [dotvm_core_lib, dotvm_cli_lib],
        dependencies: [cli11_dep, benchmark_dep],
    )
endif

# =============================================================================
# Benchmark Targets Summary
# =============================================================================

message('Benchmark targets:')
message('  - arithmetic_bench (Google Benchmark)')
message('  - dotvm_simd_benchmark')
message('  - dotvm_bigint_benchmark')
message('  - dotvm_crypto_benchmark')
if get_option('cli')
    message('  - dotvm_bench (CLI tool)')
endif
