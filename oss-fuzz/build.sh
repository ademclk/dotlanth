#!/bin/bash -eu
# OSS-Fuzz build script for DotLanth VM
# https://google.github.io/oss-fuzz/getting-started/new-project-guide/
#
# Environment variables provided by OSS-Fuzz:
#   $CC           - C compiler with sanitizers
#   $CXX          - C++ compiler with sanitizers
#   $CFLAGS       - C compiler flags
#   $CXXFLAGS     - C++ compiler flags
#   $LIB_FUZZING_ENGINE - Path to the fuzzing engine library
#   $OUT          - Output directory for fuzzers
#   $SRC          - Source directory
#   $WORK         - Working directory for build

cd $SRC/dotlanth

# Build directory
mkdir -p build
cd build

# Configure CMake
# We don't use DOTVM_BUILD_FUZZERS because OSS-Fuzz provides its own
# fuzzing engine through $LIB_FUZZING_ENGINE
cmake .. \
    -G Ninja \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDOTVM_BUILD_TESTS=OFF \
    -DDOTVM_BUILD_BENCHMARKS=OFF \
    -DDOTVM_BUILD_C_API=ON \
    -DDOTVM_ENABLE_JIT=OFF

# Build the core library
ninja dotvm_core dotvm_c

cd $SRC/dotlanth

# Compile fuzzers with OSS-Fuzz's fuzzing engine
# Each fuzzer links against $LIB_FUZZING_ENGINE instead of libfuzzer

# Common flags
FUZZ_FLAGS="-O2 -g -I$SRC/dotlanth/include"
FUZZ_LIBS="$LIB_FUZZING_ENGINE $SRC/dotlanth/build/libdotvm_core.a"

# Bytecode fuzzer
$CXX $CXXFLAGS $FUZZ_FLAGS \
    -o $OUT/bytecode_fuzzer \
    $SRC/dotlanth/fuzz/bytecode_fuzzer.cpp \
    $FUZZ_LIBS

# DSL fuzzer
$CXX $CXXFLAGS $FUZZ_FLAGS \
    -o $OUT/dsl_fuzzer \
    $SRC/dotlanth/fuzz/dsl_fuzzer.cpp \
    $FUZZ_LIBS

# ASM fuzzer
$CXX $CXXFLAGS $FUZZ_FLAGS \
    -o $OUT/asm_fuzzer \
    $SRC/dotlanth/fuzz/asm_fuzzer.cpp \
    $FUZZ_LIBS

# Execute fuzzer
$CXX $CXXFLAGS $FUZZ_FLAGS \
    -o $OUT/execute_fuzzer \
    $SRC/dotlanth/fuzz/execute_fuzzer.cpp \
    $FUZZ_LIBS

# C API fuzzer
$CXX $CXXFLAGS $FUZZ_FLAGS \
    -o $OUT/capi_fuzzer \
    $SRC/dotlanth/fuzz/capi_fuzzer.cpp \
    $FUZZ_LIBS $SRC/dotlanth/build/libdotvm_c.so

# Copy seed corpus
for fuzzer in bytecode dsl asm execute; do
    corpus_dir=$SRC/dotlanth/fuzz/corpus/$fuzzer
    if [ -d "$corpus_dir" ]; then
        zip -q -j $OUT/${fuzzer}_fuzzer_seed_corpus.zip $corpus_dir/*
    fi
done

# Copy the bytecode corpus for capi_fuzzer too
if [ -d "$SRC/dotlanth/fuzz/corpus/bytecode" ]; then
    cp $OUT/bytecode_fuzzer_seed_corpus.zip $OUT/capi_fuzzer_seed_corpus.zip
fi

echo "Build complete. Fuzzers:"
ls -la $OUT/*_fuzzer
