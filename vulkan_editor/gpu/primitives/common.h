// Common includes and utilities for primitives implementation files
#pragma once

#include "../primitives.h"
#include "../../util/logger.h"
#include "../../io/primitive_generator.h"
#include <vulkan/vk_enum_string_helper.h>
#include <vkDuck/library.h>
#include <algorithm>
#include <cassert>
#include <format>
#include <iterator>
#include <print>
#include <ranges>
#include <unordered_set>
#include <utility>
#include <vulkan/vulkan_core.h>

namespace primitives {

// Sanitize a name for use as a C++ identifier (replace spaces with underscores, etc.)
inline std::string sanitizeName(const std::string& name) {
    std::string result = name;
    std::replace(result.begin(), result.end(), ' ', '_');
    return result;
}

} // namespace primitives
