#include "SimpleFileDialog.h"
#include "../external/tinyfiledialogs.h"
#include <filesystem>
#include <string>
#include <iostream>

namespace cr::utils {

std::string FileDialogs::SelectDirectory(const char* title)
{
    const char* path = tinyfd_selectFolderDialog(title, "");
    return path ? std::string(path) : "";
}

std::string FileDialogs::OpenFile(
    const char* title,
    const char* filterDesc,
    const char* const* filters,
    int filterCount)
{
    const char* path = tinyfd_openFileDialog(
        title,
        "",
        0,          // no filters
        nullptr,    // no filters
        nullptr,    // no description
        0           // single select
    );

    return path ? std::string(path) : "";
}

std::string FileDialogs::SaveFile(
    const char* title,
    const char* defaultName,
    const char* filterDesc,
    const char* const* filters,
    int filterCount)
{
    const char* path = tinyfd_saveFileDialog(
        title,
        defaultName,
        0,          // no filters
        nullptr,    // no filters
        nullptr     // no description
    );

    if (!path) {
        return "";
    }

    if (!std::string(path).empty()) {
        // ---- Auto append .json if missing ----
        if (std::filesystem::path(path).extension().empty()) {
            std::string(path) += ".json";
        }
    }

    return path ? std::string(path) : "";
}

}
