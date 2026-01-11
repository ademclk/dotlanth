# Dotlanth

[![CI](https://github.com/YOUR_ORG/dotlanth/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_ORG/dotlanth/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/YOUR_ORG/dotlanth/branch/main/graph/badge.svg)](https://codecov.io/gh/YOUR_ORG/dotlanth)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)](https://en.cppreference.com/w/cpp/26)
[![License](https://img.shields.io/badge/License-Proprietary-red.svg)](LICENSE)

AI-Powered Automation Platform

## Overview

Dotlanth is a modular automation platform designed for building intelligent workflows. It provides a unified runtime for executing tasks, managing state, and orchestrating autonomous agents—suitable for individual developers, teams, and organizations of any size.

### Core Components

- **DotVM** - High-performance virtual machine for executing automation workflows with sandboxed execution and resource management
- **DotDB** - Integrated data layer for state persistence, caching, and cross-workflow data sharing
- **DotAgent** - Autonomous agent framework with configurable decision-making, human-in-the-loop oversight, and multi-agent coordination

### Key Features

- **Workflow Orchestration** - Define, schedule, and monitor complex multi-step automation pipelines
- **Plugin Architecture** - Extend functionality with custom plugins and integrations
- **Event-Driven Execution** - Trigger workflows based on events, schedules, or external signals
- **Observability** - Built-in logging, metrics, and tracing for debugging and monitoring

## Requirements

- **C++26 Compiler**: GCC 14+ or Clang 19+
- **CMake**: 3.28+
- **Ninja** (recommended) or Make

## Building

### Quick Start

```bash
cmake -B build -G Ninja
cmake --build build
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `DOTVM_BUILD_TESTS` | ON | Build unit tests |
| `DOTVM_BUILD_BENCHMARKS` | OFF | Build benchmarks |
| `DOTVM_ENABLE_SIMD` | ON | Enable SIMD optimizations |
| `DOTVM_ENABLE_SANITIZERS` | OFF | Enable AddressSanitizer/UBSan (Debug) |
| `DOTVM_ENABLE_COVERAGE` | OFF | Enable code coverage instrumentation |

### Build Configurations

**Release build (default):**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**Debug build with sanitizers:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDOTVM_ENABLE_SANITIZERS=ON
cmake --build build
```

**Build with coverage:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DDOTVM_ENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
lcov --directory build --capture --output-file coverage.info
```

## Running

```bash
./build/dotlanth
```

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run tests with verbose output
ctest --test-dir build --output-on-failure --verbose

# Run specific test
./build/dotvm_tests --gtest_filter="ValueTest.*"
```

## Development

### Pre-commit Hooks

Install pre-commit hooks for code quality checks:

```bash
pip install pre-commit
pre-commit install
```

Run on all files:
```bash
pre-commit run --all-files
```

### Code Formatting

The project uses clang-format for consistent code style:

```bash
# Format all files
find include src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i

# Check formatting (CI uses this)
find include src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror
```

### Static Analysis

The project uses clang-tidy for static analysis:

```bash
# Generate compile_commands.json
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Run clang-tidy
clang-tidy -p build include/dotvm/core/*.hpp
```

## Project Structure

```
dotlanth/
├── include/dotvm/core/    # Public header files
│   ├── value.hpp          # NaN-boxed value type system
│   ├── instruction.hpp    # Instruction encoding/decoding
│   ├── register_file.hpp  # Register file management
│   ├── memory.hpp         # Memory manager with handles
│   ├── vm_context.hpp     # VM execution context
│   ├── bytecode.hpp       # Bytecode format and validation
│   ├── alu.hpp            # Arithmetic Logic Unit
│   ├── cfi.hpp            # Control Flow Integrity
│   ├── concepts.hpp       # C++20 concepts
│   └── result.hpp         # Enhanced Result type
├── src/dotvm/core/        # Implementation files
├── tests/dotvm/core/      # Test suite
├── .github/workflows/     # CI configuration
├── .clang-format          # Code formatting rules
├── .clang-tidy            # Static analysis rules
├── .pre-commit-config.yaml # Pre-commit hooks
├── CMakeLists.txt         # Build configuration
└── README.md
```

## Architecture

### DotVM Core

- **Value System**: NaN-boxed 64-bit values supporting Float, Integer (48-bit), Bool, Handle, Nil, and Pointer types
- **Instruction Set**: 32-bit fixed-format instructions with three encoding types (A, B, C)
- **Memory Management**: Generation-based handle system preventing use-after-free vulnerabilities
- **Security**: Control Flow Integrity (CFI), bounds checking, and comprehensive validation

## License

Proprietary
