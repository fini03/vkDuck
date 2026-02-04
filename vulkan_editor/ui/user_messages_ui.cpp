#include "user_messages_ui.h"
#include "vulkan_editor/util/logger.h"
#include <chrono>

static std::string
formatTime(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = *std::localtime(&time);
    char buffer[16];
    std::snprintf(
        buffer, sizeof(buffer), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min,
        tm.tm_sec
    );
    return buffer;
}

void UserMessagesUI::Draw() {
    auto& logger = Logger::instance();
    auto& entries = logger.getEntries();

    // Count warnings and errors
    size_t warningCount = 0;
    size_t errorCount = 0;
    for (const auto& entry : entries) {
        if (entry.dismissed)
            continue;
        if (entry.level == LogLevel::Warning)
            ++warningCount;
        if (entry.level == LogLevel::Error)
            ++errorCount;
    }

    // Header with counts
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Messages");
    ImGui::SameLine();

    if (errorCount > 0) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(%zu errors)", errorCount
        );
    }
    if (warningCount > 0) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "(%zu warnings)",
            warningCount
        );
    }
    if (errorCount == 0 && warningCount == 0) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "(no issues)"
        );
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::Button("Clear All")) {
        // Mark all warnings/errors as dismissed
        for (auto& entry : entries) {
            if (entry.level == LogLevel::Warning ||
                entry.level == LogLevel::Error) {
                entry.dismissed = true;
            }
        }
    }

    ImGui::Separator();

    // Messages list
    ImGui::BeginChild("MessagesScrollRegion", ImVec2(0, 0), false);

    bool hasVisibleMessages = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto& entry = entries[i];

        // Only show warnings and errors
        if (entry.level != LogLevel::Warning &&
            entry.level != LogLevel::Error)
            continue;

        if (entry.dismissed)
            continue;

        hasVisibleMessages = true;

        ImGui::PushID(static_cast<int>(i));

        // Icon and color based on level
        ImVec4 color;
        const char* icon = "";

        // Style based on severity
        switch (entry.level) {
        case LogLevel::Error:
            color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            icon = "[ERROR]";
            break;
        case LogLevel::Warning:
            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            icon = "[WARN] ";
            break;
        case LogLevel::Info:
            color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
            icon = "[INFO] ";
            break;
        case LogLevel::Debug:
            color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            icon = "[DEBUG]";
            break;
        }

        // Timestamp
        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s",
            formatTime(entry.timestamp).c_str()
        );
        ImGui::SameLine();

        // Icon
        ImGui::TextColored(color, "%s", icon);
        ImGui::SameLine();

        // Category and message
        if (!entry.category.empty()) {
            ImGui::TextColored(
                color, "[%s] %s", entry.category.c_str(),
                entry.message.c_str()
            );
        } else {
            ImGui::TextColored(color, "%s", entry.message.c_str());
        }

        // Dismiss button on same line (right-aligned)
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
        if (ImGui::SmallButton("Dismiss")) {
            entry.dismissed = true;
        }

        ImGui::Separator();
        ImGui::PopID();
    }

    if (!hasVisibleMessages) {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(0.5f, 0.7f, 0.5f, 1.0f),
            "No warnings or errors to display."
        );
        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
            "Issues will appear here when they occur."
        );
    }

    ImGui::EndChild();
}
