/// @file dotvm.hpp
/// @brief Main umbrella header for the DotVM virtual machine library.
///
/// This header provides a single include point for all core DotVM functionality.
/// Include this header to access the complete DotVM API including registers,
/// memory management, instruction decoding, and VM execution context.
///
/// @code
/// #include <dotvm/dotvm.hpp>
/// using namespace dotvm::core;
/// @endcode

#pragma once

// Core module
#include "core/alu.hpp"
#include "core/arch_config.hpp"
#include "core/arch_types.hpp"
#include "core/bytecode.hpp"
#include "core/fwd.hpp"
#include "core/instruction.hpp"
#include "core/memory.hpp"
#include "core/memory_config.hpp"
#include "core/register_conventions.hpp"
#include "core/register_file.hpp"
#include "core/value.hpp"
#include "core/vm_context.hpp"
