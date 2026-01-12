#pragma once

/// @file mock_register_file.hpp
/// @brief Mock register file for testing
///
/// Provides a simple mock implementation that satisfies RegisterFileInterface
/// concept, useful for unit testing components in isolation.

#include <dotvm/core/value.hpp>
#include <dotvm/core/arch_config.hpp>
#include <dotvm/core/concepts/register_file_concept.hpp>

#include <array>
#include <vector>
#include <cstddef>

namespace dotvm::test {

/// Mock register file for testing purposes.
///
/// Features:
/// - Configurable size (default 256)
/// - Records all read/write operations for verification
/// - Satisfies RegisterFileInterface concept
/// - No architecture masking (raw values stored)
class MockRegisterFile {
public:
    using RegIdx = std::uint8_t;

    /// Access log entry for verifying test behavior
    struct AccessLog {
        enum class Type { Read, Write };
        Type type;
        RegIdx index;
        core::Value value;  // Value read or written
    };

    /// Construct with default 256 registers
    MockRegisterFile() : registers_(256, core::Value::nil()) {}

    /// Construct with specific size
    explicit MockRegisterFile(std::size_t size)
        : registers_(size, core::Value::nil()) {}

    /// Read a register value
    [[nodiscard]] core::Value read(RegIdx idx) const noexcept {
        if (idx >= registers_.size()) {
            return core::Value::nil();
        }
        auto val = registers_[idx];
        access_log_.push_back({AccessLog::Type::Read, idx, val});
        return val;
    }

    /// Write a value to a register
    void write(RegIdx idx, core::Value val) noexcept {
        if (idx >= registers_.size()) {
            return;
        }
        registers_[idx] = val;
        access_log_.push_back({AccessLog::Type::Write, idx, val});
    }

    /// Get number of registers
    [[nodiscard]] std::size_t size() const noexcept {
        return registers_.size();
    }

    /// Clear all registers to nil
    void reset() noexcept {
        for (auto& reg : registers_) {
            reg = core::Value::nil();
        }
        access_log_.clear();
    }

    // ========== Test Helpers ==========

    /// Get the access log for verification
    [[nodiscard]] const std::vector<AccessLog>& access_log() const noexcept {
        return access_log_;
    }

    /// Clear the access log
    void clear_log() noexcept {
        access_log_.clear();
    }

    /// Get read count for a specific register
    [[nodiscard]] std::size_t read_count(RegIdx idx) const noexcept {
        std::size_t count = 0;
        for (const auto& entry : access_log_) {
            if (entry.type == AccessLog::Type::Read && entry.index == idx) {
                ++count;
            }
        }
        return count;
    }

    /// Get write count for a specific register
    [[nodiscard]] std::size_t write_count(RegIdx idx) const noexcept {
        std::size_t count = 0;
        for (const auto& entry : access_log_) {
            if (entry.type == AccessLog::Type::Write && entry.index == idx) {
                ++count;
            }
        }
        return count;
    }

    /// Get total access count
    [[nodiscard]] std::size_t total_accesses() const noexcept {
        return access_log_.size();
    }

private:
    std::vector<core::Value> registers_;
    mutable std::vector<AccessLog> access_log_;
};

// Verify concept satisfaction
static_assert(core::concepts::RegisterFileInterface<MockRegisterFile>,
              "MockRegisterFile must satisfy RegisterFileInterface");

/// Architecture-aware mock register file.
///
/// Like MockRegisterFile but applies architecture masking on writes.
class MockArchRegisterFile {
public:
    using RegIdx = std::uint8_t;

    explicit MockArchRegisterFile(core::Architecture arch = core::Architecture::Arch64)
        : arch_(arch), registers_(256, core::Value::nil()) {}

    [[nodiscard]] core::Value read(RegIdx idx) const noexcept {
        if (idx >= registers_.size()) {
            return core::Value::nil();
        }
        return registers_[idx];
    }

    void write(RegIdx idx, core::Value val) noexcept {
        if (idx >= registers_.size()) {
            return;
        }
        // Apply architecture masking for integers
        if (val.is_integer()) {
            auto masked = core::arch_config::mask_int(val.as_integer(), arch_);
            registers_[idx] = core::Value::from_int(masked);
        } else {
            registers_[idx] = val;
        }
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return registers_.size();
    }

    [[nodiscard]] core::Architecture arch() const noexcept {
        return arch_;
    }

    void reset() noexcept {
        for (auto& reg : registers_) {
            reg = core::Value::nil();
        }
    }

private:
    core::Architecture arch_;
    std::vector<core::Value> registers_;
};

// Verify concept satisfaction
static_assert(core::concepts::RegisterFileInterface<MockArchRegisterFile>,
              "MockArchRegisterFile must satisfy RegisterFileInterface");
static_assert(core::concepts::ArchAwareRegisterFile<MockArchRegisterFile>,
              "MockArchRegisterFile must satisfy ArchAwareRegisterFile");

} // namespace dotvm::test
