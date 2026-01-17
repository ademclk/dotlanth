/// @file permission.cpp
/// @brief SEC-002 Permission Model implementation

#include <sstream>

#include <dotvm/core/security/permission.hpp>

namespace dotvm::core::security {

// ============================================================================
// to_string Implementation
// ============================================================================

std::string to_string(Permission perm) {
    if (perm == Permission::None) {
        return "None";
    }

    if (perm == Permission::Full) {
        return "Full";
    }

    std::ostringstream oss;
    bool first = true;

    auto append = [&](Permission p, const char* name) {
        if (has_permission(perm, p)) {
            if (!first) {
                oss << " | ";
            }
            oss << name;
            first = false;
        }
    };

    append(Permission::Execute, "Execute");
    append(Permission::ReadMemory, "ReadMemory");
    append(Permission::WriteMemory, "WriteMemory");
    append(Permission::Allocate, "Allocate");
    append(Permission::ReadState, "ReadState");
    append(Permission::WriteState, "WriteState");
    append(Permission::SpawnDot, "SpawnDot");
    append(Permission::SendMessage, "SendMessage");
    append(Permission::Crypto, "Crypto");
    append(Permission::SystemCall, "SystemCall");
    append(Permission::Debug, "Debug");

    // Handle any remaining unknown bits
    const std::uint32_t known_bits = static_cast<std::uint32_t>(Permission::Execute) |
                                     static_cast<std::uint32_t>(Permission::ReadMemory) |
                                     static_cast<std::uint32_t>(Permission::WriteMemory) |
                                     static_cast<std::uint32_t>(Permission::Allocate) |
                                     static_cast<std::uint32_t>(Permission::ReadState) |
                                     static_cast<std::uint32_t>(Permission::WriteState) |
                                     static_cast<std::uint32_t>(Permission::SpawnDot) |
                                     static_cast<std::uint32_t>(Permission::SendMessage) |
                                     static_cast<std::uint32_t>(Permission::Crypto) |
                                     static_cast<std::uint32_t>(Permission::SystemCall) |
                                     static_cast<std::uint32_t>(Permission::Debug);

    const std::uint32_t unknown = static_cast<std::uint32_t>(perm) & ~known_bits;
    if (unknown != 0) {
        if (!first) {
            oss << " | ";
        }
        oss << "Unknown(0x" << std::hex << unknown << ")";
    }

    return oss.str();
}

// ============================================================================
// PermissionDeniedException Implementation
// ============================================================================

PermissionDeniedException::PermissionDeniedException(Permission required, Permission actual,
                                                     std::string_view context,
                                                     std::source_location location)
    : required_(required), actual_(actual), context_(context), location_(location) {
    build_message();
}

void PermissionDeniedException::build_message() {
    std::ostringstream oss;
    oss << "Permission denied: required [" << to_string(required_) << "], had ["
        << to_string(actual_) << "], missing [" << to_string(missing()) << "]";

    if (!context_.empty()) {
        oss << " in context '" << context_ << "'";
    }

    oss << " at " << location_.file_name() << ":" << location_.line();

    message_ = oss.str();
}

// ============================================================================
// PermissionSet::require Implementation
// ============================================================================

void PermissionSet::require(Permission perm, std::string_view context,
                            std::source_location location) const {
    if (!has_permission(perm)) {
        throw PermissionDeniedException(perm, permissions_, context, location);
    }
}

}  // namespace dotvm::core::security
