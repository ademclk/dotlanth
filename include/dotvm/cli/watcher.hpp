#pragma once

/// @file watcher.hpp
/// @brief DSL-003 File system watcher
///
/// Watches directories for file changes and triggers recompilation.

#include <atomic>
#include <filesystem>
#include <functional>
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

/// @brief File system watcher
///
/// Monitors a directory for file changes and invokes callbacks.
class Watcher {
public:
    /// @brief Construct a watcher for a directory
    /// @param directory Directory to watch
    /// @param callback Function to call on changes
    /// @param extensions File extensions to watch (e.g., ".dsl")
    Watcher(std::filesystem::path directory, WatchCallback callback,
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

    /// @brief Get the watched directory
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    void watch_loop();

    std::filesystem::path directory_;
    WatchCallback callback_;
    std::vector<std::string> extensions_;
    std::atomic<bool> running_{false};
    std::thread watch_thread_;
};

}  // namespace dotvm::cli
