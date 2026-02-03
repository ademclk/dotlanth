// SPDX-License-Identifier: MIT
// Replication Message Fuzzer
//
// Targets Raft message deserialization including:
// - 16-byte message header parsing
// - CRC32 validation
// - Message type dispatch
// - Payload parsing for each message type

#include <cstdint>
#include <cstddef>
#include <span>

#include "dotvm/core/state/replication/message_serializer.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Skip inputs that are too small to be a valid message
    if (size < 16) {
        return 0;
    }

    std::span<const uint8_t> input{data, size};

    // Attempt to deserialize as various message types
    try {
        // Try parsing as a generic message first
        auto result = dotvm::core::state::replication::MessageSerializer::deserialize(input);

        // If successful, validate the message structure
        if (result) {
            // Access message fields to ensure they're properly initialized
            [[maybe_unused]] auto type = result->type();
            [[maybe_unused]] auto term = result->term();
            [[maybe_unused]] auto node_id = result->sender_id();
        }
    } catch (...) {
        // Exceptions are expected for malformed input - that's fine
    }

    return 0;
}
