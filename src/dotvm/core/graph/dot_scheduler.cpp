#include "dotvm/core/graph/dot_scheduler.hpp"

#include <limits>

namespace dotvm::core::graph {

DotScheduler::DotScheduler(SchedulerConfig config) noexcept
    : config_(config),
      // Graph max_nodes is set to a very large value because the graph accumulates
      // terminal nodes that can't be immediately removed (DependencyGraph requires
      // no dependents for removal). The scheduler's active_count check in submit()
      // is the real capacity control for pending/ready/running dots.
      // Using SIZE_MAX / 2 to avoid any overflow issues.
      graph_(DependencyGraphConfig{.detect_cycles_on_add = true,
                                   .max_nodes = std::numeric_limits<std::size_t>::max() / 2}) {}

DotScheduler::Result<DotHandle> DotScheduler::submit(
    std::span<const std::uint8_t> bytecode,
    std::span<const DotHandle> dependencies,
    std::int32_t priority) noexcept {
    // Validate inputs before acquiring lock
    if (bytecode.empty()) {
        return SchedulerError::InvalidBytecode;
    }

    if (priority < config_.min_priority || priority > config_.max_priority) {
        return SchedulerError::InvalidPriority;
    }

    std::unique_lock lock(mutex_);

    if (shutting_down_) {
        return SchedulerError::ShuttingDown;
    }

    // Check capacity
    const std::size_t active_count = pending_count_ + ready_count_ + running_count_;
    if (active_count >= config_.max_pending) {
        return SchedulerError::MaxPendingExceeded;
    }

    // Validate all dependencies exist and are not stale
    for (const DotHandle& dep : dependencies) {
        if (!validate_handle_unlocked(dep)) {
            return SchedulerError::DependencyNotFound;
        }
        const ScheduledDot& dep_dot = dots_[dep.index];
        if (dep_dot.generation != dep.generation) {
            return SchedulerError::DependencyNotFound;
        }
        // Cannot depend on a cancelled Dot
        if (dep_dot.state == DotState::Cancelled) {
            return SchedulerError::DependencyNotFound;
        }
    }

    // Copy bytecode and allocate slot
    std::vector<std::uint8_t> bytecode_copy(bytecode.begin(), bytecode.end());
    DotHandle handle = allocate_slot_unlocked(std::move(bytecode_copy), priority);

    ScheduledDot& dot = dots_[handle.index];

    // Assign a graph ID and add node
    dot.graph_id = next_graph_id_++;
    auto add_result = graph_.add_node(dot.graph_id);
    if (!add_result.is_ok()) {
        // Rollback: free the slot
        free_indices_.push_back(handle.index);
        dots_[handle.index].generation = 0;  // Invalidate
        --total_submitted_;
        --pending_count_;
        return SchedulerError::InternalGraphError;
    }

    // Register in O(1) lookup map
    graph_id_to_index_[dot.graph_id] = handle.index;

    // Add edges for dependencies
    for (const DotHandle& dep : dependencies) {
        const ScheduledDot& dep_dot = dots_[dep.index];
        auto edge_result = graph_.add_edge(dot.graph_id, dep_dot.graph_id);
        if (!edge_result.is_ok()) {
            // Rollback: remove node and free slot
            (void)graph_.remove_node(dot.graph_id);
            free_indices_.push_back(handle.index);
            dots_[handle.index].generation = 0;
            --total_submitted_;
            --pending_count_;

            if (edge_result.error() == DependencyGraphError::CycleDetected) {
                return SchedulerError::CycleDetected;
            }
            return SchedulerError::InternalGraphError;
        }
    }

    // Check if immediately ready (no pending dependencies)
    // Use graph state directly instead of scanning ready_nodes
    auto state_result = graph_.get_state(dot.graph_id);
    if (state_result.is_ok() && state_result.value() == NodeState::Pending) {
        // Check if all dependencies are complete (pending_dep_count == 0)
        const auto ready_nodes = graph_.get_ready();
        for (DotId ready_id : ready_nodes) {
            if (ready_id == dot.graph_id) {
                transition_to_ready_unlocked(handle.index);
                break;
            }
        }
    }

    return handle;
}

DotScheduler::Result<DotState> DotScheduler::get_state(DotHandle handle) const noexcept {
    std::shared_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    return dot.state;
}

DotScheduler::Result<std::span<const std::uint8_t>> DotScheduler::get_bytecode(
    DotHandle handle) const noexcept {
    std::shared_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    // Bytecode can be retrieved for Running dots (workers need it)
    // Also allow for Done/Failed for inspection
    if (dot.state == DotState::Pending || dot.state == DotState::Cancelled) {
        return SchedulerError::DotNotRunning;
    }

    return std::span<const std::uint8_t>(dot.bytecode);
}

std::optional<DotHandle> DotScheduler::try_pop_ready() noexcept {
    std::unique_lock lock(mutex_);

    while (!ready_queue_.empty()) {
        ReadyEntry entry = ready_queue_.top();
        ready_queue_.pop();

        // Validate the handle is still valid and ready
        if (!validate_handle_unlocked(entry.handle)) {
            continue;
        }

        ScheduledDot& dot = dots_[entry.handle.index];
        if (dot.generation != entry.handle.generation) {
            continue;  // Stale entry
        }

        if (dot.state != DotState::Ready) {
            continue;  // No longer ready (maybe cancelled)
        }

        // Transition to Running
        update_stats_for_transition_unlocked(DotState::Ready, DotState::Running);
        dot.state = DotState::Running;

        // Mark in progress in the graph
        (void)graph_.mark_in_progress(dot.graph_id);

        return entry.handle;
    }

    return std::nullopt;
}

DotScheduler::Result<void> DotScheduler::notify_complete(DotHandle handle) noexcept {
    std::unique_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (dot.state == DotState::Done || dot.state == DotState::Failed) {
        return SchedulerError::DotAlreadyCompleted;
    }

    if (dot.state == DotState::Cancelled) {
        return SchedulerError::DotAlreadyCancelled;
    }

    if (dot.state != DotState::Running) {
        return SchedulerError::DotNotRunning;
    }

    // Transition to Done
    update_stats_for_transition_unlocked(DotState::Running, DotState::Done);
    dot.state = DotState::Done;

    // Notify the graph that this node is complete
    auto graph_result = graph_.notify_complete(dot.graph_id);
    if (!graph_result.is_ok()) {
        // This shouldn't happen if state tracking is correct, but handle it
        // by still completing (state is already Done)
    }

    // Check for newly ready dependents using O(1) lookup
    const auto ready_nodes = graph_.get_ready();
    for (DotId ready_id : ready_nodes) {
        auto it = graph_id_to_index_.find(ready_id);
        if (it != graph_id_to_index_.end()) {
            std::uint32_t idx = it->second;
            if (dots_[idx].generation != 0 && dots_[idx].state == DotState::Pending) {
                transition_to_ready_unlocked(idx);
            }
        }
    }

    // Remove completed node from graph to free up capacity
    // (ScheduledDot remains for state queries)
    (void)graph_.remove_node(dot.graph_id);
    graph_id_to_index_.erase(dot.graph_id);

    // Notify waiters
    completion_cv_.notify_all();

    return Result<void>{};
}

DotScheduler::Result<void> DotScheduler::notify_failed(DotHandle handle) noexcept {
    std::unique_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (dot.state == DotState::Done || dot.state == DotState::Failed) {
        return SchedulerError::DotAlreadyCompleted;
    }

    if (dot.state == DotState::Cancelled) {
        return SchedulerError::DotAlreadyCancelled;
    }

    if (dot.state != DotState::Running) {
        return SchedulerError::DotNotRunning;
    }

    // Transition to Failed
    update_stats_for_transition_unlocked(DotState::Running, DotState::Failed);
    dot.state = DotState::Failed;

    // Propagate failure to dependents (cancel them)
    propagate_failure_to_dependents_unlocked(dot.graph_id);

    // Remove failed node from graph to free up capacity
    (void)graph_.remove_node(dot.graph_id);
    graph_id_to_index_.erase(dot.graph_id);

    // Notify waiters
    completion_cv_.notify_all();

    return Result<void>{};
}

DotScheduler::Result<void> DotScheduler::wait(DotHandle handle) noexcept {
    std::shared_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (is_terminal_state(dot.state)) {
        if (dot.state == DotState::Failed) {
            return SchedulerError::ExecutionFailed;
        }
        return Result<void>{};
    }

    // Upgrade to unique lock for condition variable wait
    lock.unlock();
    std::unique_lock ulock(mutex_);

    // Re-validate after reacquiring lock
    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    completion_cv_.wait(ulock, [this, handle]() {
        if (!validate_handle_with_generation_unlocked(handle)) {
            return true;  // Handle invalid or stale, stop waiting
        }
        return is_terminal_state(dots_[handle.index].state);
    });

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    const ScheduledDot& final_dot = dots_[handle.index];
    if (final_dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (final_dot.state == DotState::Failed) {
        return SchedulerError::ExecutionFailed;
    }

    return Result<void>{};
}

DotScheduler::Result<void> DotScheduler::cancel(DotHandle handle) noexcept {
    std::unique_lock lock(mutex_);

    if (!validate_handle_unlocked(handle)) {
        return SchedulerError::HandleNotFound;
    }

    ScheduledDot& dot = dots_[handle.index];
    if (dot.generation != handle.generation) {
        return SchedulerError::StaleHandle;
    }

    if (dot.state == DotState::Done || dot.state == DotState::Failed) {
        return SchedulerError::DotAlreadyCompleted;
    }

    if (dot.state == DotState::Cancelled) {
        return SchedulerError::DotAlreadyCancelled;
    }

    // Transition to Cancelled from any non-terminal state
    const DotState old_state = dot.state;
    update_stats_for_transition_unlocked(old_state, DotState::Cancelled);
    dot.state = DotState::Cancelled;

    // Propagate cancellation to dependents
    propagate_failure_to_dependents_unlocked(dot.graph_id);

    // Remove from graph and lookup map
    // Note: We don't remove from ready_queue_ directly (lazy removal in try_pop_ready)
    (void)graph_.remove_node(dot.graph_id);
    graph_id_to_index_.erase(dot.graph_id);

    // Notify waiters
    completion_cv_.notify_all();

    return Result<void>{};
}

SchedulerStats DotScheduler::stats() const noexcept {
    std::shared_lock lock(mutex_);

    return SchedulerStats{
        .pending_count = pending_count_,
        .ready_count = ready_count_,
        .running_count = running_count_,
        .done_count = done_count_,
        .failed_count = failed_count_,
        .cancelled_count = cancelled_count_,
        .total_submitted = total_submitted_,
    };
}

void DotScheduler::shutdown() noexcept {
    std::unique_lock lock(mutex_);

    shutting_down_ = true;

    // Cancel all pending and ready Dots
    for (std::uint32_t i = 0; i < dots_.size(); ++i) {
        ScheduledDot& dot = dots_[i];
        if (dot.generation == 0) {
            continue;  // Inactive slot
        }

        if (dot.state == DotState::Pending || dot.state == DotState::Ready) {
            update_stats_for_transition_unlocked(dot.state, DotState::Cancelled);
            dot.state = DotState::Cancelled;
            (void)graph_.remove_node(dot.graph_id);
        }
    }

    // Notify all waiters
    completion_cv_.notify_all();
}

bool DotScheduler::is_shutting_down() const noexcept {
    std::shared_lock lock(mutex_);
    return shutting_down_;
}

bool DotScheduler::validate_handle_unlocked(DotHandle handle) const noexcept {
    if (handle.index >= dots_.size()) {
        return false;
    }
    if (handle.generation == 0) {
        return false;
    }
    return dots_[handle.index].generation != 0;
}

DotHandle DotScheduler::allocate_slot_unlocked(std::vector<std::uint8_t> bytecode,
                                               std::int32_t priority) noexcept {
    std::uint32_t index;
    std::uint32_t generation;

    if (!free_indices_.empty()) {
        // Reuse a free slot
        index = free_indices_.back();
        free_indices_.pop_back();
        // Increment generation for ABA protection
        generation = dots_[index].generation + 1;
        if (generation == 0) {
            generation = 1;  // Skip 0 (invalid)
        }
    } else {
        // Allocate new slot
        index = static_cast<std::uint32_t>(dots_.size());
        generation = 1;
        dots_.emplace_back();
    }

    ScheduledDot& dot = dots_[index];
    dot.bytecode = std::move(bytecode);
    dot.state = DotState::Pending;
    dot.priority = priority;
    dot.generation = generation;
    dot.graph_id = 0;  // Will be set after add_node

    ++total_submitted_;
    ++pending_count_;

    return DotHandle{index, generation};
}

void DotScheduler::transition_to_ready_unlocked(std::uint32_t index) noexcept {
    ScheduledDot& dot = dots_[index];

    if (dot.state != DotState::Pending) {
        return;  // Only transition from Pending
    }

    update_stats_for_transition_unlocked(DotState::Pending, DotState::Ready);
    dot.state = DotState::Ready;

    // Add to ready queue
    ready_queue_.push(ReadyEntry{
        .handle = DotHandle{index, dot.generation},
        .priority = dot.priority,
        .submit_order = next_submit_order_++,
    });
}

void DotScheduler::update_stats_for_transition_unlocked(DotState from, DotState to) noexcept {
    // Decrement old state counter
    switch (from) {
        case DotState::Pending:
            if (pending_count_ > 0) --pending_count_;
            break;
        case DotState::Ready:
            if (ready_count_ > 0) --ready_count_;
            break;
        case DotState::Running:
            if (running_count_ > 0) --running_count_;
            break;
        case DotState::Done:
            if (done_count_ > 0) --done_count_;
            break;
        case DotState::Failed:
            if (failed_count_ > 0) --failed_count_;
            break;
        case DotState::Cancelled:
            if (cancelled_count_ > 0) --cancelled_count_;
            break;
    }

    // Increment new state counter
    switch (to) {
        case DotState::Pending:
            ++pending_count_;
            break;
        case DotState::Ready:
            ++ready_count_;
            break;
        case DotState::Running:
            ++running_count_;
            break;
        case DotState::Done:
            ++done_count_;
            break;
        case DotState::Failed:
            ++failed_count_;
            break;
        case DotState::Cancelled:
            ++cancelled_count_;
            break;
    }
}

bool DotScheduler::is_terminal_state(DotState state) noexcept {
    return state == DotState::Done || state == DotState::Failed || state == DotState::Cancelled;
}

bool DotScheduler::validate_handle_with_generation_unlocked(DotHandle handle) const noexcept {
    if (!validate_handle_unlocked(handle)) {
        return false;
    }
    return dots_[handle.index].generation == handle.generation;
}

void DotScheduler::propagate_failure_to_dependents_unlocked(DotId graph_id) noexcept {
    // Get dependents from the graph
    auto dependents_result = graph_.get_dependents(graph_id);
    if (!dependents_result.is_ok()) {
        return;
    }

    // Cancel each dependent that is not yet terminal
    for (DotId dep_id : dependents_result.value()) {
        auto it = graph_id_to_index_.find(dep_id);
        if (it == graph_id_to_index_.end()) {
            continue;
        }

        std::uint32_t idx = it->second;
        ScheduledDot& dep_dot = dots_[idx];
        if (dep_dot.generation == 0 || is_terminal_state(dep_dot.state)) {
            continue;
        }

        // Transition to Cancelled
        const DotState old_state = dep_dot.state;
        update_stats_for_transition_unlocked(old_state, DotState::Cancelled);
        dep_dot.state = DotState::Cancelled;

        // Recursively propagate to this dot's dependents
        propagate_failure_to_dependents_unlocked(dep_dot.graph_id);
    }
}

void DotScheduler::cleanup_terminal_node_unlocked(std::uint32_t index) noexcept {
    ScheduledDot& dot = dots_[index];
    if (dot.generation == 0) {
        return;
    }

    // Remove from graph
    (void)graph_.remove_node(dot.graph_id);

    // Remove from lookup map
    graph_id_to_index_.erase(dot.graph_id);

    // Clear bytecode to free memory
    dot.bytecode.clear();
    dot.bytecode.shrink_to_fit();

    // Add to free list for slot reuse
    free_indices_.push_back(index);
}

}  // namespace dotvm::core::graph
