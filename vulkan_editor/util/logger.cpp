#include "logger.h"
#include <iomanip>
#include <iostream>
#include <sstream>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger() {
    // File logging will be initialized when setProjectRoot() is called
}

Logger::~Logger() {
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::setProjectRoot(const std::filesystem::path& root) {
    std::lock_guard<std::mutex> lock(mutex);
    projectRoot = root;
    initFileLogging();
}

void Logger::initFileLogging() {
    // Close existing log file if open
    if (logFile.is_open()) {
        logFile.close();
        fileLoggingEnabled = false;
    }

    // Need project root to be set
    if (projectRoot.empty()) {
        return;
    }

    try {
        // Create logs directory inside project root if it doesn't exist
        std::filesystem::path logsDir = projectRoot / "logs";
        if (!std::filesystem::exists(logsDir)) {
            std::filesystem::create_directories(logsDir);
        }

        // Create log file with timestamp in name
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &time_t_now);
#else
        localtime_r(&time_t_now, &tm_now);
#endif

        std::ostringstream filename;
        filename << "vulkan_editor_"
                 << std::put_time(&tm_now, "%Y%m%d_%H%M%S")
                 << ".log";

        logFilePath = logsDir / filename.str();
        logFile.open(logFilePath, std::ios::out | std::ios::app);

        if (logFile.is_open()) {
            fileLoggingEnabled = true;
            // Write header
            logFile << "=== Vulkan Editor Log Started ===" << std::endl;
            logFile << "Timestamp: " << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << std::endl;
            logFile << "=================================" << std::endl << std::endl;
            logFile.flush();
        } else {
            std::cerr << "Failed to open log file: " << logFilePath << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize file logging: " << e.what() << std::endl;
        fileLoggingEnabled = false;
    }
}

std::string Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        default:                return "UNKNOWN";
    }
}

void Logger::writeToFile(const LogEntry& entry) {
    if (!fileLoggingEnabled || !logFile.is_open()) {
        return;
    }

    auto time_t_ts = std::chrono::system_clock::to_time_t(entry.timestamp);
    std::tm tm_ts;
#ifdef _WIN32
    localtime_s(&tm_ts, &time_t_ts);
#else
    localtime_r(&time_t_ts, &tm_ts);
#endif

    logFile << "[" << std::put_time(&tm_ts, "%Y-%m-%d %H:%M:%S") << "] "
            << "[" << levelToString(entry.level) << "] ";

    if (!entry.category.empty()) {
        logFile << "[" << entry.category << "] ";
    }

    logFile << entry.message << std::endl;
    logFile.flush();
}

std::string Logger::makeDebounceKey(
    LogLevel level,
    const std::string& category,
    const std::string& message
) const {
    return std::to_string(static_cast<int>(level)) + "|" + category + "|" + message;
}

void Logger::log(
    LogLevel level,
    const std::string& category,
    const std::string& message
) {
    std::lock_guard<std::mutex> lock(mutex);

    auto now = std::chrono::system_clock::now();

    // Debounce: skip if same message was logged recently
    std::string key = makeDebounceKey(level, category, message);
    auto it = lastMessageTimes.find(key);
    if (it != lastMessageTimes.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second);
        if (elapsed < DEBOUNCE_DURATION) {
            return; // Skip duplicate message within debounce window
        }
    }
    lastMessageTimes[key] = now;

    // Create entry
    LogEntry entry{
        .level = level,
        .category = category,
        .message = message,
        .timestamp = now,
        .dismissed = false
    };

    // Add entry to in-memory log
    entries.push_back(entry);

    // Write to file
    writeToFile(entry);

    // Track category
    if (!category.empty()) {
        categories.insert(category);
    }

    // Trim if too many entries
    while (entries.size() > MAX_ENTRIES) {
        entries.pop_front();
    }
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
}

void Logger::clearPopups() {
    std::lock_guard<std::mutex> lock(mutex);
    pendingPopups.clear();
}

std::vector<PopupNotification> Logger::consumePopups() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PopupNotification> result = std::move(pendingPopups);
    pendingPopups.clear();
    return result;
}

size_t Logger::getUnreadWarningErrorCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    size_t count = 0;
    for (const auto& entry : entries) {
        if ((entry.level == LogLevel::Warning ||
             entry.level == LogLevel::Error) &&
            !entry.dismissed) {
            ++count;
        }
    }
    return count;
}
