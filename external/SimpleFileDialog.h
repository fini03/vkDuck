#pragma once
#include <string>

namespace cr::utils {

struct FileDialogs {
    static std::string SelectDirectory(const char* title);
    static std::string OpenFile(const char* title, const char* filterDesc, const char* const* filters, int filterCount);
    static std::string SaveFile(const char* title, const char* defaultName, const char* filterDesc, const char* const* filters, int filterCount);
};

}
