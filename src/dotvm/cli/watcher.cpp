/// @file watcher.cpp
/// @brief DSL-003 File system watcher implementation
///
/// Implements a polling-based file watcher that monitors directories or
/// individual files for changes. Includes debouncing to handle rapid saves.

#include "dotvm/cli/watcher.hpp"

#include <algorithm>

namespace dotvm::cli {

Watcher::Watcher(std::filesystem::path watch_path, WatchCallback callback,
                 std::vector<std::string> extensions)
    : watch_path_(std::move(watch_path)),
      callback_(std::move(callback)),
      extensions_(std::move(extensions)) {}

Watcher::~Watcher() {
    stop();
}

Watcher::Watcher(Watcher&& other) noexcept
    : watch_path_(std::move(other.watch_path_)),
      callback_(std::move(other.callback_)),
      extensions_(std::move(other.extensions_)),
      poll_interval_(other.poll_interval_),
      debounce_interval_(other.debounce_interval_),
      running_(other.running_.load()),
      file_states_(std::move(other.file_states_)) {
    other.running_.store(false);
}

Watcher& Watcher::operator=(Watcher&& other) noexcept {
    if (this != &other) {
        stop();
        watch_path_ = std::move(other.watch_path_);
        callback_ = std::move(other.callback_);
        extensions_ = std::move(other.extensions_);
        poll_interval_ = other.poll_interval_;
        debounce_interval_ = other.debounce_interval_;
        running_.store(other.running_.load());
        file_states_ = std::move(other.file_states_);
        other.running_.store(false);
    }
    return *this;
}

void Watcher::start() {
    if (running_.load()) {
        return;
    }

    // Scan initial state before starting
    scan_initial_state();

    running_.store(true);
    watch_thread_ = std::thread(&Watcher::watch_loop, this);
}

void Watcher::stop() {
    running_.store(false);
    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }
}

bool Watcher::has_watched_extension(const std::filesystem::path& path) const {
    if (extensions_.empty()) {
        return true;  // Watch all files if no extensions specified
    }

    std::string ext = path.extension().string();
    return std::ranges::find(extensions_, ext) != extensions_.end();
}

void Watcher::scan_initial_state() {
    std::lock_guard lock(state_mutex_);
    file_states_.clear();

    std::error_code ec;
    auto now = std::chrono::steady_clock::now();

    if (std::filesystem::is_regular_file(watch_path_, ec)) {
        // Watching a single file
        if (has_watched_extension(watch_path_)) {
            auto last_write = std::filesystem::last_write_time(watch_path_, ec);
            if (!ec) {
                file_states_[watch_path_] = FileState{
                    .last_modified = last_write,
                    .last_callback = now,
                };
            }
        }
    } else if (std::filesystem::is_directory(watch_path_, ec)) {
        // Watching a directory - scan for matching files
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 watch_path_, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }

            const auto& file_path = entry.path();
            if (has_watched_extension(file_path)) {
                auto last_write = std::filesystem::last_write_time(file_path, ec);
                if (!ec) {
                    file_states_[file_path] = FileState{
                        .last_modified = last_write,
                        .last_callback = now,
                    };
                }
                ec.clear();
            }
        }
    }
}

void Watcher::watch_loop() {
    while (running_.load()) {
        std::error_code ec;

        if (std::filesystem::is_regular_file(watch_path_, ec)) {
            // Watching a single file
            auto last_write = std::filesystem::last_write_time(watch_path_, ec);
            if (!ec) {
                check_file(watch_path_, last_write);
            }
        } else if (std::filesystem::is_directory(watch_path_, ec)) {
            // Watching a directory
            std::map<std::filesystem::path, std::filesystem::file_time_type> current_files;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     watch_path_, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) {
                    ec.clear();
                    continue;
                }

                if (!entry.is_regular_file(ec) || ec) {
                    ec.clear();
                    continue;
                }

                const auto& file_path = entry.path();
                if (has_watched_extension(file_path)) {
                    auto last_write = std::filesystem::last_write_time(file_path, ec);
                    if (!ec) {
                        current_files[file_path] = last_write;
                    }
                    ec.clear();
                }
            }

            // Check for modifications and new files
            for (const auto& [path, mtime] : current_files) {
                check_file(path, mtime);
            }

            // Check for deleted files
            {
                std::lock_guard lock(state_mutex_);
                std::vector<std::filesystem::path> deleted;
                for (const auto& [path, state] : file_states_) {
                    if (current_files.find(path) == current_files.end()) {
                        deleted.push_back(path);
                    }
                }
                for (const auto& path : deleted) {
                    file_states_.erase(path);
                    if (callback_) {
                        callback_(path, WatchEvent::Deleted);
                    }
                }
            }
        }

        // Sleep for the poll interval, but check running flag periodically
        // to allow for quick shutdown
        auto remaining = poll_interval_;
        constexpr auto check_interval = std::chrono::milliseconds{50};
        while (running_.load() && remaining > std::chrono::milliseconds{0}) {
            auto sleep_time = std::min(remaining, check_interval);
            std::this_thread::sleep_for(sleep_time);
            remaining -= sleep_time;
        }
    }
}

void Watcher::check_file(const std::filesystem::path& path,
                         std::filesystem::file_time_type current_time) {
    std::lock_guard lock(state_mutex_);
    auto now = std::chrono::steady_clock::now();

    auto it = file_states_.find(path);
    if (it == file_states_.end()) {
        // New file
        file_states_[path] = FileState{
            .last_modified = current_time,
            .last_callback = now,
        };
        if (callback_) {
            callback_(path, WatchEvent::Created);
        }
    } else if (it->second.last_modified != current_time) {
        // File modified - check debounce
        auto time_since_last = now - it->second.last_callback;
        if (time_since_last >= debounce_interval_) {
            it->second.last_modified = current_time;
            it->second.last_callback = now;
            if (callback_) {
                callback_(path, WatchEvent::Modified);
            }
        }
        // If debounce not elapsed, update modification time but don't trigger callback
        // The next poll will see the same modification time and won't trigger again
        else {
            it->second.last_modified = current_time;
        }
    }
}

bool Watcher::debounce_elapsed(const std::filesystem::path& path) const {
    std::lock_guard lock(state_mutex_);
    auto it = file_states_.find(path);
    if (it == file_states_.end()) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto time_since_last = now - it->second.last_callback;
    return time_since_last >= debounce_interval_;
}

}  // namespace dotvm::cli
