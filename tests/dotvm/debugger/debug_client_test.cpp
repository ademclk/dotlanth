/// @file debug_client_test.cpp
/// @brief Unit tests for DebugClient - TOOL-011 Debug Client

#include <gtest/gtest.h>

#include "dotvm/core/instruction.hpp"
#include "dotvm/core/vm_context.hpp"
#include "dotvm/debugger/debug_client.hpp"
#include "dotvm/exec/dispatch_macros.hpp"
#include "dotvm/exec/execution_engine.hpp"

namespace dotvm::debugger {
namespace {

class DebugClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_ = core::VmConfig::for_arch(core::Architecture::Arch64);
        vm_ctx_ = std::make_unique<core::VmContext>(config_);
        engine_ = std::make_unique<exec::ExecutionEngine>(*vm_ctx_);

        DebugClientOptions opts{
            .colors = false, .show_hex = true, .show_decimal = true, .disasm_count = 5};
        client_ = std::make_unique<DebugClient>(*engine_, *vm_ctx_, opts);

        // Simple program: MOVI R1, 42; HALT
        code_[0] = core::encode_type_b(exec::opcode::MOVI, 1, 42);
        code_[1] = core::encode_type_c(exec::opcode::HALT, 0);

        client_->load_bytecode(code_.data(), code_.size(), 0, {});
    }

    core::VmConfig config_;
    std::unique_ptr<core::VmContext> vm_ctx_;
    std::unique_ptr<exec::ExecutionEngine> engine_;
    std::unique_ptr<DebugClient> client_;
    std::array<std::uint32_t, 16> code_{};
};

TEST_F(DebugClientTest, InitialState) {
    EXPECT_TRUE(client_->active());
    EXPECT_EQ(client_->session().state(), SessionState::NotStarted);
}

TEST_F(DebugClientTest, HelpCommand) {
    auto result = client_->execute_command("help");
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.should_quit);
    EXPECT_TRUE(result.output.find("run") != std::string::npos);
    EXPECT_TRUE(result.output.find("continue") != std::string::npos);
}

TEST_F(DebugClientTest, HelpAlias) {
    auto result = client_->execute_command("h");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("run") != std::string::npos);
}

TEST_F(DebugClientTest, QuitCommand) {
    auto result = client_->execute_command("quit");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.should_quit);
    EXPECT_FALSE(client_->active());
}

TEST_F(DebugClientTest, QuitAlias) {
    auto result = client_->execute_command("q");
    EXPECT_TRUE(result.should_quit);
}

TEST_F(DebugClientTest, EmptyCommand) {
    auto result = client_->execute_command("");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.empty());
}

TEST_F(DebugClientTest, UnknownCommand) {
    auto result = client_->execute_command("foobar");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.output.find("Unknown command") != std::string::npos);
}

TEST_F(DebugClientTest, SetBreakpoint) {
    auto result = client_->execute_command("b 0");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("Breakpoint 1") != std::string::npos);
    EXPECT_EQ(client_->breakpoints().count(), 1);
}

TEST_F(DebugClientTest, SetBreakpointLongForm) {
    auto result = client_->execute_command("breakpoint set --address 0x10");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(client_->breakpoints().count(), 1);
}

TEST_F(DebugClientTest, ListBreakpointsEmpty) {
    auto result = client_->execute_command("bl");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("No breakpoints") != std::string::npos);
}

TEST_F(DebugClientTest, ListBreakpoints) {
    (void)client_->execute_command("b 0");
    (void)client_->execute_command("b 1");

    auto result = client_->execute_command("bl");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("1:") != std::string::npos);
    EXPECT_TRUE(result.output.find("2:") != std::string::npos);
}

TEST_F(DebugClientTest, DeleteBreakpoint) {
    (void)client_->execute_command("b 0");
    auto result = client_->execute_command("bd 1");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(client_->breakpoints().count(), 0);
}

TEST_F(DebugClientTest, DeleteNonexistentBreakpoint) {
    auto result = client_->execute_command("bd 999");
    EXPECT_FALSE(result.success);
}

TEST_F(DebugClientTest, EnableDisableBreakpoint) {
    (void)client_->execute_command("b 0");

    auto disable_result = client_->execute_command("bdi 1");
    EXPECT_TRUE(disable_result.success);
    EXPECT_FALSE(client_->breakpoints().get(1)->enabled);

    auto enable_result = client_->execute_command("be 1");
    EXPECT_TRUE(enable_result.success);
    EXPECT_TRUE(client_->breakpoints().get(1)->enabled);
}

TEST_F(DebugClientTest, RegisterCommand) {
    auto result = client_->execute_command("reg");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("R0") != std::string::npos);
}

TEST_F(DebugClientTest, RegisterAlias) {
    auto result = client_->execute_command("reg");
    EXPECT_TRUE(result.success);
}

TEST_F(DebugClientTest, RegisterSpecific) {
    auto result = client_->execute_command("register read r1 r2");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("R1") != std::string::npos);
    EXPECT_TRUE(result.output.find("R2") != std::string::npos);
}

TEST_F(DebugClientTest, DisassembleCommand) {
    auto result = client_->execute_command("dis");
    EXPECT_TRUE(result.success);
    // Should show some disassembly
    EXPECT_FALSE(result.output.empty());
}

TEST_F(DebugClientTest, DisassembleWithCount) {
    auto result = client_->execute_command("dis --count 2");
    EXPECT_TRUE(result.success);
}

TEST_F(DebugClientTest, BacktraceCommand) {
    auto result = client_->execute_command("bt");
    EXPECT_TRUE(result.success);
}

TEST_F(DebugClientTest, SetWatchpoint) {
    auto result = client_->execute_command("w 0 0 4");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("Watchpoint 1") != std::string::npos);
    EXPECT_EQ(client_->watches().count(), 1);
}

TEST_F(DebugClientTest, ListWatchpointsEmpty) {
    auto result = client_->execute_command("wl");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.output.find("No watchpoints") != std::string::npos);
}

TEST_F(DebugClientTest, DeleteWatchpoint) {
    (void)client_->execute_command("w 0 0 4");
    auto result = client_->execute_command("wd 1");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(client_->watches().count(), 0);
}

TEST_F(DebugClientTest, SourceCommand) {
    auto result = client_->execute_command("l");
    EXPECT_TRUE(result.success);
    // Source mapping not implemented yet
    EXPECT_TRUE(result.output.find("not available") != std::string::npos);
}

TEST_F(DebugClientTest, RunCommand) {
    auto result = client_->execute_command("run");
    EXPECT_TRUE(result.success);
    // Program should complete successfully (MOVI + HALT)
    EXPECT_TRUE(result.output.find("completed") != std::string::npos ||
                result.output.find("Success") != std::string::npos);
    EXPECT_EQ(client_->session().state(), SessionState::Halted);
}

TEST_F(DebugClientTest, RunAlias) {
    auto result = client_->execute_command("r");
    EXPECT_TRUE(result.success);
}

TEST_F(DebugClientTest, StepCommand) {
    auto result = client_->execute_command("s");
    EXPECT_TRUE(result.success);
    // Should step to first instruction
    EXPECT_TRUE(result.output.find("Stepped") != std::string::npos);
    // After step, we're either paused or halted (if program completed in one step)
    EXPECT_TRUE(client_->session().state() == SessionState::Paused ||
                client_->session().state() == SessionState::Halted);
}

TEST_F(DebugClientTest, StepAlias) {
    auto result = client_->execute_command("s");
    EXPECT_TRUE(result.success);
}

TEST_F(DebugClientTest, ContinueAfterStep) {
    // Set breakpoint so we can step and then continue
    (void)client_->execute_command("b 0");
    (void)client_->execute_command("r");          // Run until breakpoint
    auto result = client_->execute_command("c");  // Continue
    // Either succeeds or program already halted
    EXPECT_TRUE(result.success || client_->session().state() == SessionState::Halted);
}

TEST_F(DebugClientTest, ContinueNotStarted) {
    // Continue on a not-started program should fail
    auto result = client_->execute_command("c");
    // Either fails with message or throws (caught by test framework)
    EXPECT_FALSE(result.success);
}

TEST_F(DebugClientTest, Prompt) {
    auto prompt = client_->prompt();
    EXPECT_TRUE(prompt.find("dotdb") != std::string::npos);
}

TEST_F(DebugClientTest, PromptAfterStep) {
    // Set breakpoint at instruction 0 so we pause there
    (void)client_->execute_command("b 0");
    (void)client_->execute_command("r");
    auto prompt = client_->prompt();
    // Should show PC or halted status
    EXPECT_TRUE(prompt.find("0x") != std::string::npos ||
                prompt.find("halted") != std::string::npos);
}

TEST_F(DebugClientTest, RegisterCustomAlias) {
    client_->register_alias("rr", "run");
    auto result = client_->execute_command("rr");
    EXPECT_TRUE(result.success);
}

// Test breakpoint hits during run
TEST_F(DebugClientTest, BreakpointHit) {
    // Set breakpoint at instruction 1 (HALT)
    (void)client_->execute_command("b 1");

    auto result = client_->execute_command("run");
    EXPECT_TRUE(result.success);
    // Should hit breakpoint before HALT
    EXPECT_TRUE(result.output.find("Breakpoint") != std::string::npos);
    EXPECT_EQ(client_->session().state(), SessionState::Paused);
}

TEST_F(DebugClientTest, FrameSelectInvalid) {
    auto result = client_->execute_command("f 99");
    EXPECT_FALSE(result.success);
}

}  // namespace
}  // namespace dotvm::debugger
