#include <dotvm/core/register_file.hpp>

namespace dotvm::core {

// Most operations are constexpr and defined in the header.
// This file is reserved for:
// 1. Non-constexpr optimizations (SIMD operations)
// 2. Debug/trace instrumentation
// 3. Future extensions

} // namespace dotvm::core
