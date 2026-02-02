/// @file debug_session.cpp
/// @brief TOOL-011 Debug Client - Session state management implementation

#include "dotvm/debugger/debug_session.hpp"

#include <stdexcept>

namespace dotvm::debugger {

void DebugSession::start() {
    if (state_ == SessionState::Halted) {
        throw std::runtime_error("Cannot start a halted session - reset first");
    }
    state_ = SessionState::Running;
    pause_reason_ = PauseReason::None;
    hit_breakpoint_id_.reset();
    triggered_watchpoint_id_.reset();
}

void DebugSession::pause(std::size_t pc, PauseReason reason) {
    state_ = SessionState::Paused;
    pc_ = pc;
    pause_reason_ = reason;
}

void DebugSession::halt() {
    state_ = SessionState::Halted;
    pause_reason_ = PauseReason::None;
}

void DebugSession::resume() {
    if (state_ != SessionState::Paused) {
        throw std::runtime_error("Cannot resume - not paused");
    }
    state_ = SessionState::Running;
    pause_reason_ = PauseReason::None;
    hit_breakpoint_id_.reset();
    triggered_watchpoint_id_.reset();
}

void DebugSession::reset() {
    state_ = SessionState::NotStarted;
    pause_reason_ = PauseReason::None;
    pc_ = 0;
    hit_breakpoint_id_.reset();
    triggered_watchpoint_id_.reset();
    call_stack_.clear();
    selected_frame_ = 0;
}

void DebugSession::set_hit_breakpoint(std::uint32_t id) {
    hit_breakpoint_id_ = id;
}

void DebugSession::set_triggered_watchpoint(std::uint32_t id) {
    triggered_watchpoint_id_ = id;
}

void DebugSession::update_call_stack(std::vector<StackFrame> frames) {
    call_stack_ = std::move(frames);
    // Reset selected frame if it's out of bounds
    if (selected_frame_ >= call_stack_.size()) {
        selected_frame_ = 0;
    }
}

bool DebugSession::select_frame(std::size_t frame_index) {
    if (frame_index >= call_stack_.size()) {
        return false;
    }
    selected_frame_ = frame_index;
    return true;
}

}  // namespace dotvm::debugger
