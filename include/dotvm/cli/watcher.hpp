#pragma once

/// @file watcher.hpp
/// @brief DSL-003 File system watcher
///
/// Watches directories for file changes and triggers recompilation.
/// Implements a polling-based watcher (cross-platform) with optional
/// inotify optimization on Linux.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dotvm::cli {

/// @brief Event types for file changes
enum class WatchEvent {
    Created,
    Modified,
    Deleted,
};

/// @brief Callback type for file change notifications
using WatchCallback = std::function<void(const std::filesystem::path&, WatchEvent)>;

/// @brief File system watcher using polling
///
/// Monitors a directory or file for changes by periodically checking
/// file modification times. Includes debouncing to prevent multiple
/// callbacks for rapid successive changes.
class Watcher {
public:
    /// @brief Default poll interval (500ms)
    static constexpr auto kDefaultPollInterval = std::chrono::milliseconds{500};

    /// @brief Default debounce interval (100ms)
    static constexpr auto kDefaultDebounceInterval = std::chrono::milliseconds{100};

    /// @brief Construct a watcher for a directory or file
    /// @param watch_path Path to watch (directory or file)
    /// @param callback Function to call on changes
    /// @param extensions File extensions to watch (e.g., ".dsl")
    Watcher(std::filesystem::path watch_path, WatchCallback callback,
            std::vector<std::string> extensions = {".dsl"});

    /// @brief Destructor - stops watching
    ~Watcher();

    // Non-copyable
    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    // Movable
    Watcher(Watcher&&) noexcept;
    Watcher& operator=(Watcher&&) noexcept;

    /// @brief Start watching (non-blocking)
    void start();

    /// @brief Stop watching
    void stop();

    /// @brief Check if currently watching
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

    /// @brief Get the watched path
    [[nodiscard]] const std::filesystem::path& watch_path() const noexcept { return watch_path_; }

    /// @brief Set the poll interval
    /// @param interval Time between file system checks
    void set_poll_interval(std::chrono::milliseconds interval) noexcept {
        poll_interval_ = interval;
    }

    /// @brief Get the poll interval
    [[nodiscard]] std::chrono::milliseconds poll_interval() const noexcept {
        return poll_interval_;
    }

    /// @brief Set the debounce interval
    /// @param interval Minimum time between callbacks for the same file
    void set_debounce_interval(std::chrono::milliseconds interval) noexcept {
        debounce_interval_ = interval;
    }

    /// @brief Get the debounce interval
    [[nodiscard]] std::chrono::milliseconds debounce_interval() const noexcept {
        return debounce_interval_;
    }

    /// @brief Check if a file has a watched extension
    /// @param path File path to check
    /// @return true if the file extension matches one of the watched extensions
    [[nodiscard]] bool has_watched_extension(const std::filesystem::path& path) const;

private:
    /// @brief File state for tracking changes
    struct FileState {
        std::filesystem::file_time_type last_modified;
        std::chrono::steady_clock::time_point last_callback;
    };

    /// @brief The main watch loop (runs in background thread)
    void watch_loop();

    /// @brief Scan directory for files and populate initial state
    void scan_initial_state();

    /// @brief Check a single file for changes
    /// @param path File path
    /// @param current_time Current modification time
    void check_file(const std::filesystem::path& path,
                    std::filesystem::file_time_type current_time);

    /// @brief Check if debounce period has elapsed
    /// @param path File path
    /// @return true if enough time has passed since last callback
    [[nodiscard]] bool debounce_elapsed(const std::filesystem::path& path) const;

    std::filesystem::path watch_path_;
    WatchCallback callback_;
    std::vector<std::string> extensions_;

    std::chrono::milliseconds poll_interval_{kDefaultPollInterval};
    std::chrono::milliseconds debounce_interval_{kDefaultDebounceInterval};

    std::atomic<bool> running_{false};
    std::thread watch_thread_;

    mutable std::mutex state_mutex_;
    std::map<std::filesystem::path, FileState> file_states_;
};

}  // namespace dotvm::cli
