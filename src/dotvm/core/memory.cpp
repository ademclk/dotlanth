#include <dotvm/core/memory.hpp>

// Platform detection for memory allocation
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
    #include <sys/mman.h>
    #define DOTVM_USE_MMAP 1
#elif defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define DOTVM_USE_VIRTUALALLOC 1
#else
    #include <cstdlib>
    #define DOTVM_USE_ALIGNED_ALLOC 1
#endif

namespace dotvm::core {

// ========== Platform-specific allocation ==========

void* MemoryManager::os_allocate(std::size_t size) noexcept {
#if DOTVM_USE_MMAP
    void* ptr = mmap(nullptr, size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    return (ptr == MAP_FAILED) ? nullptr : ptr;

#elif DOTVM_USE_VIRTUALALLOC
    return VirtualAlloc(nullptr, size,
                        MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);

#else
    return std::aligned_alloc(mem_config::ALLOCATION_ALIGNMENT, size);
#endif
}

void MemoryManager::os_deallocate(void* ptr, std::size_t size) noexcept {
    if (!ptr) return;

#if DOTVM_USE_MMAP
    munmap(ptr, size);

#elif DOTVM_USE_VIRTUALALLOC
    (void)size;  // VirtualFree doesn't need size
    VirtualFree(ptr, 0, MEM_RELEASE);

#else
    (void)size;  // std::free doesn't need size
    std::free(ptr);
#endif
}

// ========== MemoryManager implementation ==========

MemoryManager::~MemoryManager() {
    // Free all active allocations
    for (std::uint32_t i = 0; i < table_.capacity(); ++i) {
        if (table_.is_valid_index(i)) {
            const auto& entry = table_[i];
            if (entry.is_active && entry.ptr) {
                os_deallocate(entry.ptr, entry.size);
            }
        }
    }
}

MemoryManager::Result<Handle> MemoryManager::allocate(std::size_t size) noexcept {
    // Validate size
    if (size == 0 || size > max_allocation_size_) {
        return {invalid_handle(), MemoryError::InvalidSize};
    }

    // Round up to page boundary
    std::size_t aligned_size = align_to_page(size);

    // Allocate a slot in the handle table
    std::uint32_t index = table_.allocate_slot();
    if (index == mem_config::INVALID_INDEX) {
        return {invalid_handle(), MemoryError::HandleTableFull};
    }

    // Allocate memory from OS
    void* ptr = os_allocate(aligned_size);
    if (!ptr) {
        table_.release_slot(index);
        return {invalid_handle(), MemoryError::AllocationFailed};
    }

    // Set up the entry
    auto& entry = table_[index];
    entry.ptr = ptr;
    entry.size = aligned_size;
    // Generation was set when slot was allocated

    total_allocated_ += aligned_size;

    Handle h{
        .index = index,
        .generation = entry.generation
    };

    return {h, MemoryError::Success};
}

MemoryError MemoryManager::deallocate(Handle h) noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return err;
    }

    auto& entry = table_[h.index];

    // Free the memory
    if (entry.ptr) {
        os_deallocate(entry.ptr, entry.size);
        total_allocated_ -= entry.size;
    }

    // Release the slot (increments generation)
    table_.release_slot(h.index);

    return MemoryError::Success;
}

MemoryManager::Result<void*> MemoryManager::get_ptr(Handle h) noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return {nullptr, err};
    }

    return {table_[h.index].ptr, MemoryError::Success};
}

MemoryManager::Result<const void*> MemoryManager::get_ptr(Handle h) const noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return {nullptr, err};
    }

    return {table_[h.index].ptr, MemoryError::Success};
}

MemoryError MemoryManager::write_bytes(Handle h, std::size_t offset,
                                       const void* src, std::size_t count) noexcept {
    if (!src || count == 0) {
        return MemoryError::Success;  // No-op for null/zero
    }

    auto err = validate_bounds(h, offset, count);
    if (err != MemoryError::Success) {
        return err;
    }

    auto [ptr, ptr_err] = get_ptr(h);
    if (ptr_err != MemoryError::Success) {
        return ptr_err;
    }

    std::memcpy(static_cast<char*>(ptr) + offset, src, count);
    return MemoryError::Success;
}

MemoryError MemoryManager::read_bytes(Handle h, std::size_t offset,
                                      void* dst, std::size_t count) const noexcept {
    if (!dst || count == 0) {
        return MemoryError::Success;  // No-op for null/zero
    }

    auto err = validate_bounds(h, offset, count);
    if (err != MemoryError::Success) {
        return err;
    }

    auto [ptr, ptr_err] = get_ptr(h);
    if (ptr_err != MemoryError::Success) {
        return ptr_err;
    }

    std::memcpy(dst, static_cast<const char*>(ptr) + offset, count);
    return MemoryError::Success;
}

MemoryManager::Result<std::size_t> MemoryManager::get_size(Handle h) const noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return {0, err};
    }

    return {table_[h.index].size, MemoryError::Success};
}

bool MemoryManager::is_valid(Handle h) const noexcept {
    return table_.is_valid_handle(h);
}

MemoryError MemoryManager::validate_handle(Handle h) const noexcept {
    if (!table_.is_valid_handle(h)) {
        return MemoryError::InvalidHandle;
    }
    return MemoryError::Success;
}

MemoryError MemoryManager::validate_bounds(Handle h, std::size_t offset,
                                           std::size_t size) const noexcept {
    auto err = validate_handle(h);
    if (err != MemoryError::Success) {
        return err;
    }

    const auto& entry = table_[h.index];

    // Check for overflow
    if (offset > entry.size || size > entry.size - offset) {
        return MemoryError::BoundsViolation;
    }

    return MemoryError::Success;
}

} // namespace dotvm::core
