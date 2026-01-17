/// @file jit_code_buffer.cpp
/// @brief Implementation of executable memory management

#include "dotvm/jit/jit_code_buffer.hpp"

#include <cstring>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
    #include <sys/mman.h>
    #include <unistd.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

namespace dotvm::jit {

namespace {

/// @brief Round up to next page boundary
[[nodiscard]] std::size_t round_up_to_page(std::size_t size) noexcept {
    const std::size_t page = JitCodeBuffer::page_size();
    return (size + page - 1) & ~(page - 1);
}

/// @brief Round up to alignment boundary
[[nodiscard]] std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

#if defined(__unix__) || defined(__APPLE__)

/// @brief Allocate memory with mmap
[[nodiscard]] std::uint8_t* allocate_memory(std::size_t size) noexcept {
    void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) [[unlikely]] {
        return nullptr;
    }
    return static_cast<std::uint8_t*>(ptr);
}

/// @brief Free memory with munmap
void free_memory(std::uint8_t* ptr, std::size_t size) noexcept {
    if (ptr != nullptr) {
        ::munmap(ptr, size);
    }
}

/// @brief Change memory protection with mprotect
[[nodiscard]] bool set_protection(std::uint8_t* ptr, std::size_t size,
                                  MemoryProtection prot) noexcept {
    int flags = 0;
    switch (prot) {
        case MemoryProtection::ReadWrite:
            flags = PROT_READ | PROT_WRITE;
            break;
        case MemoryProtection::ReadExecute:
            flags = PROT_READ | PROT_EXEC;
            break;
        case MemoryProtection::None:
            flags = PROT_NONE;
            break;
    }
    return ::mprotect(ptr, size, flags) == 0;
}

#elif defined(_WIN32)

/// @brief Allocate memory with VirtualAlloc
[[nodiscard]] std::uint8_t* allocate_memory(std::size_t size) noexcept {
    void* ptr = ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return static_cast<std::uint8_t*>(ptr);
}

/// @brief Free memory with VirtualFree
void free_memory(std::uint8_t* ptr, std::size_t /*size*/) noexcept {
    if (ptr != nullptr) {
        ::VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

/// @brief Change memory protection with VirtualProtect
[[nodiscard]] bool set_protection(std::uint8_t* ptr, std::size_t size,
                                  MemoryProtection prot) noexcept {
    DWORD flags = 0;
    switch (prot) {
        case MemoryProtection::ReadWrite:
            flags = PAGE_READWRITE;
            break;
        case MemoryProtection::ReadExecute:
            flags = PAGE_EXECUTE_READ;
            break;
        case MemoryProtection::None:
            flags = PAGE_NOACCESS;
            break;
    }
    DWORD old_flags;
    return ::VirtualProtect(ptr, size, flags, &old_flags) != 0;
}

#else
    #error "Unsupported platform for JIT code buffer"
#endif

}  // anonymous namespace

// Static method to get page size
std::size_t JitCodeBuffer::page_size() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    static const std::size_t cached = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    return cached;
#elif defined(_WIN32)
    static const std::size_t cached = []() {
        SYSTEM_INFO si;
        ::GetSystemInfo(&si);
        return static_cast<std::size_t>(si.dwPageSize);
    }();
    return cached;
#endif
}

// Factory method
CodeBufferResult<JitCodeBuffer> JitCodeBuffer::create(std::size_t size) {
    if (size == 0) [[unlikely]] {
        return std::unexpected(CodeBufferError::InvalidParameters);
    }

    // Round up to page boundary
    const std::size_t actual_size = round_up_to_page(size);

    // Allocate memory
    std::uint8_t* base = allocate_memory(actual_size);
    if (base == nullptr) [[unlikely]] {
        return std::unexpected(CodeBufferError::AllocationFailed);
    }

    return JitCodeBuffer(base, actual_size);
}

// Private constructor
JitCodeBuffer::JitCodeBuffer(std::uint8_t* base, std::size_t capacity) noexcept
    : base_(base), capacity_(capacity), used_(0), protection_(MemoryProtection::ReadWrite) {}

// Destructor
JitCodeBuffer::~JitCodeBuffer() {
    free_memory(base_, capacity_);
}

// Move constructor
JitCodeBuffer::JitCodeBuffer(JitCodeBuffer&& other) noexcept
    : base_(std::exchange(other.base_, nullptr)),
      capacity_(std::exchange(other.capacity_, 0)),
      used_(std::exchange(other.used_, 0)),
      protection_(std::exchange(other.protection_, MemoryProtection::None)) {}

// Move assignment
JitCodeBuffer& JitCodeBuffer::operator=(JitCodeBuffer&& other) noexcept {
    if (this != &other) {
        // Free current memory
        free_memory(base_, capacity_);

        // Take ownership of other's memory
        base_ = std::exchange(other.base_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        used_ = std::exchange(other.used_, 0);
        protection_ = std::exchange(other.protection_, MemoryProtection::None);
    }
    return *this;
}

// Allocate region within buffer
CodeBufferResult<std::span<std::uint8_t>> JitCodeBuffer::allocate(std::size_t size,
                                                                  std::size_t alignment) noexcept {
    if (protection_ != MemoryProtection::ReadWrite) [[unlikely]] {
        return std::unexpected(CodeBufferError::NotWritable);
    }

    if (size == 0) [[unlikely]] {
        return std::unexpected(CodeBufferError::InvalidParameters);
    }

    // Align the current position
    const std::size_t aligned_offset = align_up(used_, alignment);

    // Check if we have enough space
    if (aligned_offset + size > capacity_) [[unlikely]] {
        return std::unexpected(CodeBufferError::InsufficientSpace);
    }

    // Update used and return span
    std::uint8_t* ptr = base_ + aligned_offset;
    used_ = aligned_offset + size;

    return std::span<std::uint8_t>(ptr, size);
}

// Make buffer executable
CodeBufferResult<void> JitCodeBuffer::make_executable() noexcept {
    if (protection_ == MemoryProtection::ReadExecute) {
        return {};  // Already executable
    }

    if (!set_protection(base_, capacity_, MemoryProtection::ReadExecute)) [[unlikely]] {
        return std::unexpected(CodeBufferError::ProtectionFailed);
    }

    protection_ = MemoryProtection::ReadExecute;
    return {};
}

// Make buffer writable
CodeBufferResult<void> JitCodeBuffer::make_writable() noexcept {
    if (protection_ == MemoryProtection::ReadWrite) {
        return {};  // Already writable
    }

    if (!set_protection(base_, capacity_, MemoryProtection::ReadWrite)) [[unlikely]] {
        return std::unexpected(CodeBufferError::ProtectionFailed);
    }

    protection_ = MemoryProtection::ReadWrite;
    return {};
}

// Reset buffer
CodeBufferResult<void> JitCodeBuffer::reset() noexcept {
    if (protection_ != MemoryProtection::ReadWrite) [[unlikely]] {
        return std::unexpected(CodeBufferError::NotWritable);
    }

    used_ = 0;
    return {};
}

// WritableGuard implementation
WritableGuard::WritableGuard(JitCodeBuffer& buffer)
    : buffer_(buffer), original_(buffer.protection()), success_(false) {
    if (original_ == MemoryProtection::ReadWrite) {
        success_ = true;
        return;
    }

    auto result = buffer_.make_writable();
    success_ = result.has_value();
}

WritableGuard::~WritableGuard() {
    if (!success_) {
        return;
    }

    // Restore original protection
    if (original_ == MemoryProtection::ReadExecute) {
        (void)buffer_.make_executable();
    }
}

}  // namespace dotvm::jit
