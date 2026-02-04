#include "debug_console_ui.h"
#include "vulkan_editor/util/logger.h"
#include <algorithm>
#include <chrono>
#include <vector>

// Static member initialization
bool DebugConsoleUI::showDebug = true;
bool DebugConsoleUI::showInfo = true;
bool DebugConsoleUI::showWarning = true;
bool DebugConsoleUI::showError = true;
bool DebugConsoleUI::autoScroll = true;
char DebugConsoleUI::searchFilter[256] = "";
int DebugConsoleUI::selectedCategory = 0;

static const char* getLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "?";
}

static ImVec4 getLevelColor(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    case LogLevel::Info:
        return ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
    case LogLevel::Warning:
        return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    case LogLevel::Error:
        return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

static std::string
formatTimestamp(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  tp.time_since_epoch()
              ) %
              1000;

    std::tm tm = *std::localtime(&time);
    char buffer[32];
    std::snprintf(
        buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", tm.tm_hour,
        tm.tm_min, tm.tm_sec, static_cast<int>(ms.count())
    );
    return buffer;
}

void DebugConsoleUI::Draw() {
    auto& logger = Logger::instance();
    const auto& entries = logger.getEntries();
    const auto& categories = logger.getCategories();

    // Build category list
    std::vector<std::string> categoryList;
    categoryList.push_back("All");
    for (const auto& cat : categories) {
        categoryList.push_back(cat);
    }

    // Top controls bar
    ImGui::PushStyleColor(
        ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f)
    );

    // Level filters
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Levels:");
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warning", &showWarning);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Category filter
    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo(
            "Category", categoryList[selectedCategory].c_str()
        )) {
        for (size_t i = 0; i < categoryList.size(); ++i) {
            bool isSelected = (selectedCategory == static_cast<int>(i));
            if (ImGui::Selectable(
                    categoryList[i].c_str(), isSelected
                )) {
                selectedCategory = static_cast<int>(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Search filter
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint(
        "##search", "Search...", searchFilter, sizeof(searchFilter)
    );

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Auto-scroll toggle
    ImGui::Checkbox("Auto-scroll", &autoScroll);

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // Clear button
    if (ImGui::Button("Clear")) {
        logger.clear();
    }

    ImGui::SameLine();

    // Copy button
    if (ImGui::Button("Copy All")) {
        std::string allText;
        for (const auto& entry : entries) {
            allText += formatTimestamp(entry.timestamp);
            allText += " [";
            allText += getLevelName(entry.level);
            allText += "] ";
            if (!entry.category.empty()) {
                allText += "[";
                allText += entry.category;
                allText += "] ";
            }
            allText += entry.message;
            allText += "\n";
        }
        ImGui::SetClipboardText(allText.c_str());
    }

    ImGui::PopStyleColor();
    ImGui::Separator();

    // Log entries
    ImGui::BeginChild(
        "LogScrollRegion", ImVec2(0, 0), false,
        ImGuiWindowFlags_HorizontalScrollbar
    );

    std::string searchStr(searchFilter);
    std::transform(
        searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower
    );

    for (const auto& entry : entries) {
        // Level filter
        bool showByLevel = false;
        switch (entry.level) {
        case LogLevel::Debug:
            showByLevel = showDebug;
            break;
        case LogLevel::Info:
            showByLevel = showInfo;
            break;
        case LogLevel::Warning:
            showByLevel = showWarning;
            break;
        case LogLevel::Error:
            showByLevel = showError;
            break;
        }
        if (!showByLevel)
            continue;

        // Category filter
        if (selectedCategory > 0) {
            const std::string& filterCat =
                categoryList[selectedCategory];
            if (entry.category != filterCat)
                continue;
        }

        // Search filter
        if (!searchStr.empty()) {
            std::string msgLower = entry.message;
            std::transform(
                msgLower.begin(), msgLower.end(), msgLower.begin(),
                ::tolower
            );
            if (msgLower.find(searchStr) == std::string::npos)
                continue;
        }

        // Format and display
        ImVec4 color = getLevelColor(entry.level);

        ImGui::TextColored(
            ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s",
            formatTimestamp(entry.timestamp).c_str()
        );
        ImGui::SameLine();

        ImGui::TextColored(color, "[%s]", getLevelName(entry.level));
        ImGui::SameLine();

        if (!entry.category.empty()) {
            ImGui::TextColored(
                ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[%s]",
                entry.category.c_str()
            );
            ImGui::SameLine();
        }

        ImGui::TextColored(color, "%s", entry.message.c_str());
    }

    // Auto-scroll to bottom
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}
