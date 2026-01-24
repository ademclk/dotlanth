# ==============================================================================
# TOOL-011: Fuzzing Infrastructure
# ==============================================================================
#
# This file defines libFuzzer-based fuzz targets for the DotLanth VM.
# Requires Clang compiler with libFuzzer support.
#
# Build: cmake -B build -DDOTVM_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
# Run:   ./build/dsl_fuzzer corpus/dsl -max_total_time=300

cmake_minimum_required(VERSION 3.28)

# Fuzzer compile and link flags
set(FUZZER_FLAGS
    -fsanitize=fuzzer,address
    -fno-omit-frame-pointer
)

# Optional coverage instrumentation for measuring fuzzer effectiveness
if(DOTVM_ENABLE_COVERAGE)
    list(APPEND FUZZER_FLAGS
        -fprofile-instr-generate
        -fcoverage-mapping
    )
endif()

# ==============================================================================
# Helper function to create fuzz targets
# ==============================================================================

function(add_dotvm_fuzzer NAME)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs SOURCES LIBRARIES)
    cmake_parse_arguments(FUZZER "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${NAME} ${FUZZER_SOURCES})

    target_link_libraries(${NAME} PRIVATE ${FUZZER_LIBRARIES})

    # Apply fuzzer flags with optimization for better coverage
    target_compile_options(${NAME} PRIVATE
        ${FUZZER_FLAGS}
        -O2
        -g
    )
    target_link_options(${NAME} PRIVATE ${FUZZER_FLAGS})

    # Set output directory to keep fuzzers organized
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fuzz"
    )
endfunction()

# ==============================================================================
# Fuzz Targets
# ==============================================================================

# Bytecode header parsing fuzzer (existing)
add_dotvm_fuzzer(bytecode_fuzzer
    SOURCES bytecode_fuzzer.cpp
    LIBRARIES dotvm_core
)

# DSL parser fuzzer
add_dotvm_fuzzer(dsl_fuzzer
    SOURCES dsl_fuzzer.cpp
    LIBRARIES dotvm_core
)

# Assembly parser fuzzer
add_dotvm_fuzzer(asm_fuzzer
    SOURCES asm_fuzzer.cpp
    LIBRARIES dotvm_core
)

# Execution engine fuzzer
add_dotvm_fuzzer(execute_fuzzer
    SOURCES execute_fuzzer.cpp
    LIBRARIES dotvm_core
)

# C API fuzzer (only if C API is enabled)
if(DOTVM_BUILD_C_API)
    add_dotvm_fuzzer(capi_fuzzer
        SOURCES capi_fuzzer.cpp
        LIBRARIES dotvm_c
    )
endif()

# ==============================================================================
# Custom targets for convenience
# ==============================================================================

# Target to build all fuzzers
add_custom_target(fuzzers
    DEPENDS
        bytecode_fuzzer
        dsl_fuzzer
        asm_fuzzer
        execute_fuzzer
        $<$<BOOL:${DOTVM_BUILD_C_API}>:capi_fuzzer>
    COMMENT "Building all fuzz targets"
)

message(STATUS "Fuzzer targets enabled:")
message(STATUS "  - bytecode_fuzzer (bytecode header parsing)")
message(STATUS "  - dsl_fuzzer (DSL parser)")
message(STATUS "  - asm_fuzzer (assembly parser)")
message(STATUS "  - execute_fuzzer (execution engine)")
if(DOTVM_BUILD_C_API)
    message(STATUS "  - capi_fuzzer (C API)")
endif()
message(STATUS "Build all: cmake --build . --target fuzzers")
