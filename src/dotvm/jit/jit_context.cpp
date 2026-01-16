/// @file jit_context.cpp
/// @brief Implementation of aggregate JIT context

#include "dotvm/jit/jit_context.hpp"

namespace dotvm::jit {

std::unique_ptr<JitContext> JitContext::create() {
    return create(JitConfig::defaults());
}

std::unique_ptr<JitContext> JitContext::create(const JitConfig& config) {
    auto ctx = std::unique_ptr<JitContext>(new JitContext(config));
    if (!ctx->initialize()) {
        return nullptr;
    }
    return ctx;
}

JitContext::JitContext(const JitConfig& config)
    : config_(config)
    , profiler_(config)
    , stencils_(StencilRegistry::create_default())
    , cache_(config)
{}

JitContext::~JitContext() = default;

bool JitContext::initialize() {
    if (!config_.enabled) {
        // JIT disabled - that's OK
        initialized_ = true;
        return true;
    }

    // Allocate code buffer
    auto buffer_result = JitCodeBuffer::create(config_.max_code_cache);
    if (!buffer_result) {
        return false;
    }
    code_buffer_ = std::make_unique<JitCodeBuffer>(std::move(*buffer_result));

    initialized_ = true;
    return true;
}

// ============================================================================
// Function Registration and Profiling
// ============================================================================

FunctionId JitContext::register_function(
    std::size_t entry_pc,
    std::size_t end_pc
) {
    return profiler_.register_function(entry_pc, end_pc);
}

LoopId JitContext::register_loop(
    FunctionId func_id,
    std::size_t header_pc,
    std::size_t backedge_pc
) {
    return profiler_.register_loop(func_id, header_pc, backedge_pc);
}

bool JitContext::record_call(FunctionId func_id) noexcept {
    if (!config_.enabled) {
        return false;
    }

    (void)profiler_.record_call(func_id);
    return profiler_.should_compile(func_id);
}

bool JitContext::record_iteration(LoopId loop_id) noexcept {
    if (!config_.osr_enabled) {
        return false;
    }

    (void)profiler_.record_iteration(loop_id);
    return profiler_.should_osr(loop_id);
}

std::optional<FunctionId> JitContext::find_function(std::size_t entry_pc) const noexcept {
    return profiler_.find_function_by_pc(entry_pc);
}

std::optional<LoopId> JitContext::find_loop(std::size_t backedge_pc) const noexcept {
    return profiler_.find_loop_by_backedge(backedge_pc);
}

// ============================================================================
// Compilation
// ============================================================================

JitStatus JitContext::compile_function(
    FunctionId func_id,
    std::span<const std::uint8_t> bytecode
) {
    if (!initialized_ || !config_.enabled) {
        return JitStatus::Disabled;
    }

    if (profiler_.is_compiled(func_id)) {
        return JitStatus::Success;  // Already compiled
    }

    // Get function boundaries from profiler
    const auto* profile = profiler_.get_function(func_id);
    if (!profile) {
        return JitStatus::InvalidFunction;
    }

    // Parse bytecode into instructions
    auto instructions = parse_bytecode(bytecode, profile->entry_pc, profile->end_pc);
    if (instructions.empty()) {
        return JitStatus::InvalidFunction;
    }

    // Create compiler and compile
    JitCompiler compiler(config_, *code_buffer_, stencils_);

    auto result = compiler.compile(func_id, instructions);
    if (!result) {
        return result.error();
    }

    // Make code executable
    auto exec_result = code_buffer_->make_executable();
    if (!exec_result) {
        return JitStatus::ProtectionFailed;
    }

    // Store in cache
    if (!cache_.store(
        func_id,
        result->code,
        result->code_size,
        result->entry_pc,
        result->end_pc
    )) {
        return JitStatus::CacheFull;
    }

    // Mark as compiled in profiler
    profiler_.mark_compiled(func_id);

    return JitStatus::Success;
}

OsrStatus JitContext::compile_osr(
    LoopId loop_id,
    std::span<const std::uint8_t> bytecode
) {
    if (!config_.osr_enabled) {
        return OsrStatus::Disabled;
    }

    // For now, OSR compilation is similar to function compilation
    // but starts at the loop header instead of function entry

    const auto* loop_profile = profiler_.get_loop(loop_id);
    if (!loop_profile) {
        return OsrStatus::InvalidLoop;
    }

    // Get the containing function
    FunctionId func_id = loop_id_function(loop_id);
    const auto* func_profile = profiler_.get_function(func_id);
    if (!func_profile) {
        return OsrStatus::InvalidLoop;
    }

    // First, ensure the function is compiled
    if (!profiler_.is_compiled(func_id)) {
        auto compile_result = compile_function(func_id, bytecode);
        if (compile_result != JitStatus::Success) {
            return OsrStatus::NoEntryPoint;
        }
    }

    // Look up the compiled function
    const auto* entry = cache_.lookup(func_id);
    if (!entry) {
        return OsrStatus::NoEntryPoint;
    }

    // Register the OSR entry point
    // For a real implementation, we'd calculate the offset within
    // the compiled code that corresponds to the loop header
    // For now, we just use the function entry
    cache_.register_osr_entry(
        loop_id,
        entry->code,  // Would be entry->code + loop_offset
        loop_profile->header_pc,
        func_id
    );

    profiler_.mark_osr_triggered(loop_id);

    return OsrStatus::Success;
}

bool JitContext::is_compiled(FunctionId func_id) const noexcept {
    return profiler_.is_compiled(func_id);
}

// ============================================================================
// Execution
// ============================================================================

const CompiledEntry* JitContext::lookup(FunctionId func_id) noexcept {
    return cache_.lookup(func_id);
}

const CompiledEntry* JitContext::lookup_by_pc(std::size_t entry_pc) noexcept {
    return cache_.lookup_by_pc(entry_pc);
}

const OsrEntry* JitContext::lookup_osr(LoopId loop_id) noexcept {
    return cache_.lookup_osr(loop_id);
}

void JitContext::execute(
    const CompiledEntry* entry,
    void* regs,
    void* ctx
) noexcept {
    if (!entry || !entry->is_valid()) [[unlikely]] {
        return;
    }

    // Cast to function pointer and call
    auto fn = entry->as_function<void(void*, void*)>();
    fn(regs, ctx);
}

// ============================================================================
// Cache Management
// ============================================================================

void JitContext::invalidate(FunctionId func_id) noexcept {
    cache_.invalidate(func_id);
    // Note: We don't reset the profiler - the function might get
    // recompiled if it's still hot
}

void JitContext::invalidate_range(std::size_t start_pc, std::size_t end_pc) noexcept {
    cache_.invalidate_range(start_pc, end_pc);
}

void JitContext::clear_cache() noexcept {
    cache_.clear();
    profiler_.reset();

    // Reset the code buffer if writable
    if (code_buffer_ && code_buffer_->is_writable()) {
        (void)code_buffer_->reset();
    }
}

// ============================================================================
// Statistics
// ============================================================================

JitContext::Stats JitContext::stats() const noexcept {
    Stats s;

    // Profiler stats
    auto prof_stats = profiler_.get_stats();
    s.registered_functions = prof_stats.total_functions;
    s.registered_loops = prof_stats.total_loops;
    s.total_calls = prof_stats.total_calls;
    s.total_iterations = prof_stats.total_iterations;
    s.functions_compiled = prof_stats.compiled_functions;

    // Cache stats
    auto cache_stats = cache_.stats();
    s.cache_entries = cache_stats.entry_count;
    s.cache_bytes_used = cache_stats.total_code_bytes;
    s.cache_hits = cache_stats.hits;
    s.cache_misses = cache_stats.misses;
    s.cache_hit_rate = cache_stats.hit_rate();

    // Code buffer stats
    if (code_buffer_) {
        s.bytes_generated = code_buffer_->used();
    }

    return s;
}

} // namespace dotvm::jit
