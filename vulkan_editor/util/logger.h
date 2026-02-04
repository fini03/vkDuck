#pragma once

#include <chrono>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class LogLevel { Debug, Info, Warning, Error };

struct LogEntry {
    LogLevel level;
    std::string category;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    bool dismissed{false};
};

struct PopupNotification {
    LogLevel level;
    std::string category;
    std::string message;
};

class Logger {
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void
    log(LogLevel level,
        const std::string& category,
        const std::string& message);

    void debug(const std::string& message) {
        log(LogLevel::Debug, "", message);
    }
    void info(const std::string& message) {
        log(LogLevel::Info, "", message);
    }
    void warning(const std::string& message) {
        log(LogLevel::Warning, "", message);
    }
    void error(const std::string& message) {
        log(LogLevel::Error, "", message);
    }

    void debug(
        const std::string& category,
        const std::string& message
    ) {
        log(LogLevel::Debug, category, message);
    }
    void info(
        const std::string& category,
        const std::string& message
    ) {
        log(LogLevel::Info, category, message);
    }
    void warning(
        const std::string& category,
        const std::string& message
    ) {
        log(LogLevel::Warning, category, message);
    }
    void error(
        const std::string& category,
        const std::string& message
    ) {
        log(LogLevel::Error, category, message);
    }

    const std::deque<LogEntry>& getEntries() const {
        return entries;
    }
    std::deque<LogEntry>& getEntries() {
        return entries;
    }
    const std::unordered_set<std::string>& getCategories() const {
        return categories;
    }

    void clear();
    void clearPopups();
    std::vector<PopupNotification> consumePopups();
    size_t getUnreadWarningErrorCount() const;

    // Set project root for file logging (logs will be saved in projectRoot/logs/)
    void setProjectRoot(const std::filesystem::path& root);

    static constexpr size_t MAX_ENTRIES = 1000;
    static constexpr std::chrono::milliseconds DEBOUNCE_DURATION{500};

private:
    Logger();
    ~Logger();

    void initFileLogging();
    void writeToFile(const LogEntry& entry);
    std::string makeDebounceKey(LogLevel level, const std::string& category, const std::string& message) const;
    std::string levelToString(LogLevel level) const;

    std::deque<LogEntry> entries;
    std::unordered_set<std::string> categories;
    std::vector<PopupNotification> pendingPopups;
    std::unordered_map<std::string, std::chrono::system_clock::time_point> lastMessageTimes;
    mutable std::mutex mutex;

    // File logging
    std::ofstream logFile;
    std::filesystem::path logFilePath;
    std::filesystem::path projectRoot;
    bool fileLoggingEnabled{false};
};

namespace Log {
inline void debug(const std::string& msg) {
    Logger::instance().debug(msg);
}
inline void info(const std::string& msg) {
    Logger::instance().info(msg);
}
inline void warning(const std::string& msg) {
    Logger::instance().warning(msg);
}
inline void error(const std::string& msg) {
    Logger::instance().error(msg);
}

inline void debug(
    const std::string& cat,
    const std::string& msg
) {
    Logger::instance().debug(cat, msg);
}
inline void info(
    const std::string& cat,
    const std::string& msg
) {
    Logger::instance().info(cat, msg);
}
inline void warning(
    const std::string& cat,
    const std::string& msg
) {
    Logger::instance().warning(cat, msg);
}
inline void error(
    const std::string& cat,
    const std::string& msg
) {
    Logger::instance().error(cat, msg);
}

template <typename... Args>
void debug(
    const std::string& cat,
    std::format_string<Args...> fmt,
    Args&&... args
) {
    Logger::instance().debug(
        cat, std::format(fmt, std::forward<Args>(args)...)
    );
}
template <typename... Args>
void info(
    const std::string& cat,
    std::format_string<Args...> fmt,
    Args&&... args
) {
    Logger::instance().info(
        cat, std::format(fmt, std::forward<Args>(args)...)
    );
}
template <typename... Args>
void warning(
    const std::string& cat,
    std::format_string<Args...> fmt,
    Args&&... args
) {
    Logger::instance().warning(
        cat, std::format(fmt, std::forward<Args>(args)...)
    );
}
template <typename... Args>
void error(
    const std::string& cat,
    std::format_string<Args...> fmt,
    Args&&... args
) {
    Logger::instance().error(
        cat, std::format(fmt, std::forward<Args>(args)...)
    );
}
} // namespace Log
