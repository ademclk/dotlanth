/// @file watch_command.cpp
/// @brief DSL-003 Watch command implementation
///
/// Watches a directory or file for .dsl file changes and triggers recompilation.
/// Features clear screen, timestamps, and color-coded output.

#include "dotvm/cli/commands/watch_command.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "dotvm/cli/file_resolver.hpp"
#include "dotvm/cli/watcher.hpp"
#include "dotvm/core/dsl/compiler/dsl_compiler.hpp"
#include "dotvm/core/dsl/ir/printer.hpp"
#include "dotvm/core/dsl/parser.hpp"

namespace dotvm::cli::commands {

namespace {

/// @brief Global flag for signal handling
std::atomic<bool> g_should_stop{false};

/// @brief Signal handler for graceful shutdown
extern "C" void signal_handler(int /*signal*/) {
    g_should_stop.store(true);
}

/// @brief Format file size for display
std::string format_file_size(std::size_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024 * 1024) {
        double kb = static_cast<double>(bytes) / 1024.0;
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << kb << " KB";
        return oss.str();
    }
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << mb << " MB";
    return oss.str();
}

/// @brief Print a compile error with colored source snippet
void print_compile_error(Terminal& term, std::string_view filename, std::string_view source,
                         const core::dsl::compiler::CompileError& err) {
    std::string label = "error";
    std::string stage_prefix;

    switch (err.stage) {
        case core::dsl::compiler::CompileError::Stage::Parse:
            stage_prefix = "parse error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::IRBuild:
            stage_prefix = "ir error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Optimize:
            stage_prefix = "optimization error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Lower:
            stage_prefix = "lowering error: ";
            break;
        case core::dsl::compiler::CompileError::Stage::Codegen:
            stage_prefix = "codegen error: ";
            break;
    }

    std::string message = stage_prefix + err.message;
    term.print_error(filename, source, err.span, label, message);
}

/// @brief Write bytecode to a file
bool write_bytecode(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytecode,
                    Terminal& term) {
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file) {
        term.error("error: ");
        term.print("failed to open output file: " + path.string());
        term.newline();
        return false;
    }

    file.write(reinterpret_cast<const char*>(bytecode.data()),
               static_cast<std::streamsize>(bytecode.size()));

    if (!file.good()) {
        term.error("error: ");
        term.print("failed to write bytecode to: " + path.string());
        term.newline();
        return false;
    }

    return true;
}

/// @brief Result of processing includes
struct IncludeResult {
    bool success = true;
    ExitCode code = ExitCode::Success;
    std::string merged_source;
};

/// @brief Remove include directives from source to avoid re-parsing them
/// @param source The source code to process
/// @return Source with include directives replaced by blank lines
std::string strip_include_directives(std::string_view source) {
    std::string result;
    result.reserve(source.size());

    std::size_t pos = 0;
    while (pos < source.size()) {
        std::size_t line_start = pos;

        std::size_t content_start = pos;
        while (content_start < source.size() &&
               (source[content_start] == ' ' || source[content_start] == '\t')) {
            content_start++;
        }

        if (content_start + 8 <= source.size() && source.substr(content_start, 8) == "include:") {
            std::size_t line_end = content_start;
            while (line_end < source.size() && source[line_end] != '\n') {
                line_end++;
            }

            result.append(source.substr(line_start, content_start - line_start));
            if (line_end < source.size()) {
                result += '\n';
                pos = line_end + 1;
            } else {
                pos = line_end;
            }
        } else {
            std::size_t line_end = pos;
            while (line_end < source.size() && source[line_end] != '\n') {
                line_end++;
            }
            if (line_end < source.size()) {
                line_end++;
            }
            result.append(source.substr(pos, line_end - pos));
            pos = line_end;
        }
    }

    return result;
}

/// @brief Process includes recursively and merge sources
IncludeResult process_includes(FileResolver& resolver, const std::filesystem::path& file_path,
                               std::string_view source, Terminal& term, bool verbose, bool debug,
                               std::uint32_t include_line = 0) {
    IncludeResult result;

    if (resolver.would_create_cycle(file_path)) {
        std::string chain_info = FileError::format_include_chain(resolver.include_stack());
        if (!chain_info.empty()) {
            term.print(chain_info);
            term.newline();
        }
        term.error("error: ");
        term.print("circular include detected: " + file_path.string());
        term.newline();
        result.success = false;
        result.code = ExitCode::CircularInclude;
        return result;
    }

    resolver.push_include_stack(file_path, include_line);

    auto parse_result = core::dsl::DslParser::parse(source);
    if (!parse_result.is_ok()) {
        result.merged_source = std::string(source);
        resolver.pop_include_stack();
        return result;
    }

    const auto& module = parse_result.value();

    if (module.includes.empty()) {
        result.merged_source = std::string(source);
        resolver.pop_include_stack();
        return result;
    }

    std::string merged_includes;

    for (const auto& include : module.includes) {
        auto resolve_result = resolver.resolve_include(include.path, file_path);
        if (!resolve_result.has_value()) {
            const auto& err = resolve_result.error();
            std::string chain_info = FileError::format_include_chain(resolver.include_stack());
            if (!chain_info.empty()) {
                term.print(chain_info);
                term.newline();
            }
            term.error("error: ");
            term.print(err.message);
            term.newline();
            result.success = false;
            result.code = err.code;
            resolver.pop_include_stack();
            return result;
        }

        const auto& resolved_path = *resolve_result;

        if (resolver.is_included(resolved_path)) {
            if (debug) {
                term.info("[debug] ");
                term.print("Skipping already included: " + resolved_path.string());
                term.newline();
            }
            continue;
        }

        resolver.mark_included(resolved_path);

        if (verbose) {
            term.info("Including: ");
            term.print(resolved_path.string());
            term.newline();
        }

        auto file_result = resolver.read_file(resolved_path);
        if (!file_result.has_value()) {
            const auto& err = file_result.error();
            std::string chain_info = FileError::format_include_chain(resolver.include_stack());
            if (!chain_info.empty()) {
                term.print(chain_info);
                term.newline();
            }
            term.error("error: ");
            term.print(err.message);
            term.newline();
            result.success = false;
            result.code = err.code;
            resolver.pop_include_stack();
            return result;
        }

        auto include_result = process_includes(resolver, resolved_path, *file_result, term, verbose,
                                               debug, include.span.start.line);
        if (!include_result.success) {
            resolver.pop_include_stack();
            return include_result;
        }

        resolver.cache_file(resolved_path, include_result.merged_source);

        std::string stripped = strip_include_directives(include_result.merged_source);
        if (!merged_includes.empty() && !merged_includes.ends_with('\n')) {
            merged_includes += '\n';
        }
        merged_includes += stripped;
    }

    std::string main_stripped = strip_include_directives(source);

    if (merged_includes.empty()) {
        result.merged_source = main_stripped;
    } else {
        if (!merged_includes.ends_with('\n')) {
            merged_includes += '\n';
        }
        result.merged_source = merged_includes + main_stripped;
    }

    resolver.pop_include_stack();
    return result;
}

/// @brief Compile a single DSL file
/// @return Pair of (success, output_size_bytes)
std::pair<bool, std::size_t> compile_file(const std::filesystem::path& file_path,
                                          const GlobalOptions& global, Terminal& term,
                                          std::chrono::milliseconds& compile_time) {
    auto start_time = std::chrono::steady_clock::now();

    std::filesystem::path input_path = std::filesystem::canonical(file_path);
    FileResolver resolver(input_path.parent_path().empty() ? std::filesystem::current_path()
                                                           : input_path.parent_path());

    auto file_result = resolver.read_file(input_path);
    if (!file_result.has_value()) {
        const auto& err = file_result.error();
        term.error("error: ");
        term.print(err.message);
        term.newline();
        return {false, 0};
    }

    const std::string& source = *file_result;
    std::string filename = file_path.filename().string();

    resolver.mark_included(input_path);
    auto include_result =
        process_includes(resolver, input_path, source, term, global.verbose, global.debug);
    if (!include_result.success) {
        return {false, 0};
    }

    core::dsl::compiler::CompileOptions compile_opts;
    compile_opts.dump_ir = global.debug;

    core::dsl::compiler::DslCompiler compiler(compile_opts);
    auto compile_result = compiler.compile_source(include_result.merged_source);

    if (!compile_result.has_value()) {
        print_compile_error(term, filename, source, compile_result.error());
        return {false, 0};
    }

    const auto& result = *compile_result;

    if (global.output_ir) {
        term.info("; IR output:");
        term.newline();
        std::string ir_str = core::dsl::ir::print_to_string(result.ir);
        term.print(ir_str);
    }

    std::filesystem::path output_path = FileResolver::default_output_path(input_path);

    if (!write_bytecode(output_path, result.bytecode, term)) {
        return {false, 0};
    }

    auto end_time = std::chrono::steady_clock::now();
    compile_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return {true, result.bytecode.size()};
}

}  // anonymous namespace

void clear_screen(Terminal& term) {
    // Use ANSI escape sequence to clear screen and move cursor home
    // \033[2J - Clear entire screen
    // \033[H  - Move cursor to home position (1,1)
    term.print("\033[2J\033[H");
}

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    std::tm local_tm;
#ifdef _WIN32
    localtime_s(&local_tm, &time_t_now);
#else
    localtime_r(&time_t_now, &local_tm);
#endif

    std::ostringstream oss;
    oss << '[' << std::setfill('0') << std::setw(2) << local_tm.tm_hour << ':' << std::setw(2)
        << local_tm.tm_min << ':' << std::setw(2) << local_tm.tm_sec << ']';
    return oss.str();
}

ExitCode execute_watch(const WatchOptions& opts, const GlobalOptions& global, Terminal& term) {
    namespace fs = std::filesystem;

    // Reset the global stop flag
    g_should_stop.store(false);

    // Set up signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    fs::path watch_path;
    std::error_code ec;

    // Determine if watching a file or directory
    watch_path = fs::canonical(opts.directory, ec);
    if (ec) {
        term.error("error: ");
        term.print("cannot access path: " + opts.directory);
        term.newline();
        return ExitCode::FileNotFound;
    }

    bool is_single_file = fs::is_regular_file(watch_path, ec);
    bool is_directory = fs::is_directory(watch_path, ec);

    if (!is_single_file && !is_directory) {
        term.error("error: ");
        term.print("path is neither a file nor a directory: " + watch_path.string());
        term.newline();
        return ExitCode::FileNotFound;
    }

    // Print initial watch message
    term.info("Watching: ");
    term.print(watch_path.string());
    term.newline();
    term.print("Press Ctrl+C to stop");
    term.newline();
    term.newline();

    // Track compilation state for each file
    std::atomic<bool> compilation_in_progress{false};

    // Create the callback for file changes
    auto on_change = [&](const fs::path& changed_path, WatchEvent event) {
        // Only process modifications (not creates/deletes for now)
        if (event == WatchEvent::Deleted) {
            return;
        }

        // Prevent concurrent compilations
        if (compilation_in_progress.exchange(true)) {
            return;
        }

        // Clear screen
        clear_screen(term);

        // Print timestamp and what's being compiled
        std::string timestamp = get_timestamp();
        term.info(timestamp);
        term.print(" Compiling: ");
        term.print(changed_path.filename().string());
        term.newline();

        // Compile the file
        std::chrono::milliseconds compile_time{0};
        auto [success, output_size] = compile_file(changed_path, global, term, compile_time);

        // Print result
        std::string result_timestamp = get_timestamp();
        if (success) {
            term.info(result_timestamp);
            term.success(" [OK] ");
            term.print("Success: ");
            fs::path output_path = FileResolver::default_output_path(changed_path);
            term.print(output_path.filename().string());
            term.print(" (" + format_file_size(output_size) + ", " +
                       std::to_string(compile_time.count()) + "ms)");
            term.newline();
        } else {
            term.info(result_timestamp);
            term.error(" [FAIL] ");
            term.print("Compilation failed");
            term.newline();
        }

        term.newline();
        term.print("Watching for changes... (Ctrl+C to stop)");
        term.newline();

        compilation_in_progress.store(false);
    };

    // Create watcher
    Watcher watcher(watch_path, on_change, {".dsl"});
    watcher.set_poll_interval(std::chrono::milliseconds{500});
    watcher.set_debounce_interval(std::chrono::milliseconds{100});

    // Start watching
    watcher.start();

    // Wait for signal to stop
    while (!g_should_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    // Stop watching
    watcher.stop();

    term.newline();
    term.info("Watch mode stopped.");
    term.newline();

    return ExitCode::Success;
}

}  // namespace dotvm::cli::commands
